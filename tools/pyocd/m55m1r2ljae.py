"""Repository-local pyOCD guard rails for the Nuvoton M55M1R2LJAE.

Pass this file to pyOCD with ``--script``. It fixes the DFP omission of the
128 KiB DTCM that the vendor APROM FLM uses, while deliberately exposing only
the secure physical APROM window as programmable flash.
"""

from pathlib import Path

from pyocd.core.memory_map import FlashRegion, RamRegion


APROM_START = 0x00100000
APROM_SIZE = 0x00200000
FLASH_PAGE_SIZE = 0x00002000
DTCM_START = 0x20000000
DTCM_SIZE = 0x00020000
FLASH_ALGO_RAM_SIZE = 0x00008000
FLM_PATH = Path(__file__).resolve().with_name("M55M1_AP_2M.FLM")
FLM_SHA256 = "e8774cbfa4d3ae76edda4536f9def3df282385f67b46c5884bcae2409131278c"


def configure_memory_map(memory_map):
    """Restrict writable Flash to APROM and add the FLM's DTCM workspace."""
    for region in list(memory_map):
        if region.is_flash:
            memory_map.remove_region(region)

    if memory_map.get_region_for_address(DTCM_START) is None:
        memory_map.add_region(
            RamRegion(name="DTCM", start=DTCM_START, length=DTCM_SIZE, access="rwx")
        )

    aprom = FlashRegion(
        name="APROM_SECURE",
        start=APROM_START,
        length=APROM_SIZE,
        access="rx",
        blocksize=FLASH_PAGE_SIZE,
        page_size=FLASH_PAGE_SIZE,
        flm=str(FLM_PATH),
    )
    # pyOCD's FLM builder honours these DFP-compatible attributes when
    # selecting the algorithm workspace.
    aprom._RAMstart = DTCM_START
    aprom._RAMsize = FLASH_ALGO_RAM_SIZE
    memory_map.add_region(aprom)


def will_init_target(target, init_sequence):
    """pyOCD user-script hook, called before FlashRegion construction."""
    configure_memory_map(target.memory_map)
