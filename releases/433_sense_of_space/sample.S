.section .rodata.sample, "a"
.balign 4
.global sample_data
.global sample_data_end
sample_data:
#if FOUR33_SAMPLE_RATE == 12000
    .incbin "433_loop_12k_s8_stereo.bin"
#else
    .incbin "433_loop_10k_s8_stereo.bin"
#endif
sample_data_end:
.byte 0
