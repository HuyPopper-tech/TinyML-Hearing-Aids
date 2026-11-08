"""Active product ABI and source-package identity, independent of replay."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Complex3ContractTests(unittest.TestCase):
    def test_active_blob_is_approved_package_blob(self):
        source = (ROOT / 'models/deepfir/deepfir_model_data.cc').read_text()
        payload = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', source))
        self.assertEqual(len(payload), 516848)
        self.assertEqual(hashlib.sha256(payload).hexdigest(),
                         '69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27')

    def test_product_adapter_has_three_inputs_four_outputs(self):
        header = (ROOT / 'services/deepfir/deepfir_model_adapter.h').read_text()
        self.assertRegex(header, r'DEEPFIR_MODEL_INPUT_COUNT\s+3u')
        self.assertRegex(header, r'DEEPFIR_MODEL_OUTPUT_COUNT\s+4u')

    def test_compiled_flatbuffer_ports_match_binding_without_invoke(self):
        """Inspect serialized tensors; never instantiate an interpreter or replay."""
        binding = json.loads((ROOT / 'models/deepfir/compiled_tensor_binding.json').read_text())
        declarations = []
        for direction in ('inputs', 'outputs'):
            entries = []
            for tensor in binding[direction]:
                shape = tensor['shape'] + [0] * (4 - len(tensor['shape']))
                entries.append('{' + f'{tensor["port"]},{tensor["tensor_index"]},'
                               f'"{tensor["name"]}",{len(tensor["shape"])},'
                               '{' + ','.join(map(str, shape)) + '},'
                               f'{tensor["scale"]}f,{tensor["zero_point"]}' + '}')
            declarations.append('expected_t ' + direction + '[] = {' + ','.join(entries) + '};')
        harness = r'''
        #include <cstdint>
        #include <cstring>
        #include <fstream>
        #include <iterator>
        #include <vector>
        #include "tensorflow/lite/schema/schema_generated.h"
        struct expected_t { int port, index; const char *name; int rank, shape[4]; float scale; int zero; };
        EXPECTATIONS
        int main(int argc, char **argv) {
            if (argc != 2) return 1;
            std::ifstream stream(argv[1], std::ios::binary);
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)), {});
            flatbuffers::Verifier verifier(bytes.data(), bytes.size());
            if (!tflite::VerifyModelBuffer(verifier)) return 2;
            const auto *model = tflite::GetModel(bytes.data());
            if (model->subgraphs()->size() != 1) return 3;
            const auto *graph = model->subgraphs()->Get(0);
            for (unsigned direction = 0; direction < 2; ++direction) {
                const auto *ports = direction ? graph->outputs() : graph->inputs();
                const auto *expected = direction ? outputs : inputs;
                if (ports->size() != (direction ? 4u : 3u)) return 4;
                for (unsigned i = 0; i < ports->size(); ++i) {
                    const auto &e = expected[i];
                    if (ports->Get(e.port) != e.index) return 5;
                    const auto *t = graph->tensors()->Get(e.index);
                    if (std::strcmp(t->name()->c_str(), e.name) ||
                        t->type() != tflite::TensorType_INT16 || t->shape()->size() != e.rank) return 6;
                    for (int d = 0; d < e.rank; ++d) { if (t->shape()->Get(d) != e.shape[d]) return 7; }
                    const auto *q = t->quantization();
                    if (!q || q->scale()->size() != 1 || q->zero_point()->size() != 1 ||
                        q->scale()->Get(0) != e.scale || q->zero_point()->Get(0) != e.zero) return 8;
                }
            }
            return 0;
        }
        '''.replace('EXPECTATIONS', '\n'.join(declarations))
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            source = temporary / 'binding.cc'
            source.write_text(harness)
            blob = temporary / 'model.tflite'
            model_source = (ROOT / 'models/deepfir/deepfir_model_data.cc').read_text()
            blob.write_bytes(bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', model_source)))
            executable = temporary / 'binding.exe'
            build = subprocess.run(['g++', '-std=c++17', '-O2', '-I', str(ROOT / 'libs/tensorflow/runtime'),
                                    '-I', str(ROOT / 'libs/tensorflow/third_party/flatbuffers/include'),
                                    str(source), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable), str(blob)], capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
