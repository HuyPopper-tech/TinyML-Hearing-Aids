# Secure APROM requires Secure MEM-AP transactions and an actually halted CPU.
proc m55m1_halt {} {
    cortex_m.dap apcsw 0 0x40000000
    halt 1000
    # S_RESET_ST is sticky/read-clear; the second sample must be stable.
    read_memory 0xE000EDF0 32 1
    set dhcsr [lindex [read_memory 0xE000EDF0 32 1] 0]
    if {($dhcsr & 0x021A0000) != 0x00120000} {
        error [format "M55M1 not ready: DHCSR=0x%08X (need Secure debug + halt, no reset/lockup). Stop and cold-reset the board; CH549F cannot drive nRESET. No flash operation started." $dhcsr]
    }
    set pdid [lindex [read_memory 0x40000000 32 1] 0]
    if {$pdid != 0xA2000120} {
        error [format "Expected M55M1R2LJAE PDID 0xA2000120, got 0x%08X" $pdid]
    }
}

# Program a raw application image at the physical APROM base, then verify it.
# This entry point never accepts a destination address or a chip-erase option.
proc m55m1_program {image} {
    if {![file isfile $image] || [string tolower [file extension $image]] ne ".bin"} {
        error "Expected an existing raw .bin firmware image: $image"
    }
    set size [file size $image]
    if {$size < 8 || $size > 0x00200000} {
        error "Firmware must contain a vector table and fit in the 2 MiB APROM"
    }

    m55m1_halt
    # Start the RAM flash algorithm before application caches, MPU and DMA run.
    reset halt
    m55m1_halt
    flash probe 0
    flash write_image erase $image 0x00100000 bin
    verify_image $image 0x00100000 bin
    echo "M55M1 APROM write and verification completed; core remains halted"
}
