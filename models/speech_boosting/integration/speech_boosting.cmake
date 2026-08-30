add_library(speech_boosting_u55_vela_model STATIC
    ../models/speech_boosting/generated/speech_boosting_u55_vela_model_data.cc
)

target_include_directories(speech_boosting_u55_vela_model PUBLIC
    ../models/speech_boosting
    ../models/speech_boosting/generated
)
