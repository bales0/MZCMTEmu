# Source inventory at the start of the ESP32-S3 parity port

Generated 2026-11-12. Reference: `600a78807e0e5080ae3d59d8d9afa8d99bf3b8fb` (firmware 2.0.3). Target baseline: `5e654f1683c0436f5f8cb3853ece24e8c618dbf7`.

This is a complete file-level inventory, not a completed semantic review or proof of parity. Candidate destinations are design decisions; no module is considered ported because it appears here. All hashes describe source contents at inventory time. Reference and MZTools remain read-only.

| Reference file | Lines | SHA256 | Candidate target / disposition |
|---|---:|---|---|
| `src/drivers/backlight.cpp` | 44 | `99c53863bc5d03e3a8141fea0effdb46ac6ab0a7f89d664de82e544af31d8712` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/backlight.h` | 16 | `6b4e5f98fd8ea77f07906d119ab310dcd70928bbd5096e8e4935827f62cf4101` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/calibration_store.cpp` | 254 | `88b24b0bca63e724fd1db84b65f2e6bbace652aa1a69a93d9d1563c83deec674` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/calibration_store.h` | 46 | `8585957edd59d3595b43421c01a91d88bd41e4dfdb39e986df149858dcfc3cd8` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/external_switch.cpp` | 52 | `7fdfda1c6739a07549da1b30c180969e7dd2d2458935254c3c07b0168caa83dd` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/external_switch.h` | 20 | `037ea2d8d4502d950aa9e49c24472b09764f80c363e32da0933cd1df07c63428` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/flash_text.h` | 43 | `79a190b59bdb9288bbf100c4b0391d616770c2270cb90c67909710ad77b1b1ad` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/keypad.cpp` | 426 | `48a31c4bd04a2b05de82e78bdfaf9f2967ffd5383649907e83ac2489aefb658a` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/keypad.h` | 76 | `a9d841ac6868a7ff1f615f1f815e68ff1fe72b9db93c0ca8a424e653b2df198d` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/lcd.cpp` | 78 | `92b24f70281156d921f761812ec6e83b3d9cd8e1a2ffcf35cd452922e2c2ce25` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/lcd.h` | 19 | `449bb4fdf0ec2d63210d216813aabee182a935c36cce70972498ee39a598c428` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/monitor_mute.cpp` | 107 | `1016abf3257d7427e57c3bb4d10a6729ba8375a2ed6cf7de50b06441060951e7` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/monitor_mute.h` | 30 | `24ce7088a642418c88359c15dc4ddf4847e534d911f861220e6c18e19d7c2702` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/mzio.cpp` | 106 | `625d2c6f24b5991e2c902023d271c240e60695b048831e8f1389902e3bc82af0` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/mzio.h` | 39 | `7e6cf404353ccee6b4c4b676c3ffde04aadebb0ba7facb499c628f992ec8a62a` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/ram_monitor.cpp` | 90 | `8b91b2006a4c2b7446646a24ac352c90599b001cde4060137e21444c34c4b2a4` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/ram_monitor.h` | 17 | `35ef9418b87b2e15d5a6a86317df083e2c70ada76349246bfaf45bc762e65cdb` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/sdcard.cpp` | 1691 | `130ced0d7901d829066febf1fa718a8d03dd0390bb8f9560d781cb0f2da6f461` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/sdcard.h` | 132 | `5e0eeeeb15a2209d00fa0c432994a19aec46ff5864032d13ad3cc6dbcda3c44b` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/wav_playback_driver.cpp` | 318 | `a58b0b17fd1fafb0047be436a18bd43ffcb3ef057cb4a0893d04e20c5f4e2d78` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/wav_playback_driver.h` | 54 | `a8afef43d0fb4fb64dec1452370d9c488added6c4a7e91a2d2c915cf02f70894` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/wav_record_driver.cpp` | 329 | `e21a96af9e0f4a1c76a141c84435e5b7017205aeb0e37441eab8017b3635ad1f` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/wav_record_driver.h` | 58 | `cfe0b672170c4595a77dad532fac5836df856ed918860993b4c85ab4ad5328e8` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/write_edge_monitor.cpp` | 138 | `01ed748a53f360a203d15418b43379dd86072293767d6752ac0f042758741921` | src/hal, storage, config; replace AVR hardware |
| `src/drivers/write_edge_monitor.h` | 22 | `c32482f664eeed3fc34e506cd7ef6bb25ab536872d7e398af662e81bb3cb2820` | src/hal, storage, config; replace AVR hardware |
| `src/formats/file_format.cpp` | 70 | `8cfff7752ef1b16ea06b7da51851ef63a80e2c855cfac28fbca34f166f149979` | src/formats; portable parser/profile core |
| `src/formats/file_format.h` | 28 | `eb2916d0e93bc9ef1aeb57fb48dc21c9ba7a16e6c16a95e16821cde3e34731d2` | src/formats; portable parser/profile core |
| `src/formats/mzi_sidecar.cpp` | 557 | `d4ceb2d2da8af82c5373113d36d2d8c58edcd2c7174bace6c6ed1db9e2c6a3f8` | src/formats; portable parser/profile core |
| `src/formats/mzi_sidecar.h` | 63 | `b455fe798048aecd70c59f232e85a0bc11ef11580e6e5d4df67bbf27af797ee0` | src/formats; portable parser/profile core |
| `src/formats/mz_loader_profiles.cpp` | 178 | `9adbb955427ea83f26b175f92ce977cf50b7e547752451ad96ab5a98e282a2a1` | src/formats; portable parser/profile core |
| `src/formats/mz_loader_profiles.h` | 45 | `d0bd14e18947d0ddf2bb053b068cc353fb8260c001f7e5c53f081e5714190c32` | src/formats; portable parser/profile core |
| `src/formats/mz_tape_decoder.cpp` | 660 | `020504599a688829868983daf9cb41d3cc32fe31c7472c6dc19b96e3165a04dc` | src/formats; portable parser/profile core |
| `src/formats/mz_tape_decoder.h` | 46 | `64de3ba69e92cafb5187dbebdeab9eb2e3eb0a7286eb02d7f68b7624ffc158f4` | src/formats; portable parser/profile core |
| `src/formats/mz_tape_profiles.cpp` | 78 | `59c68b22f524ac92af1d7f2122ebb7e75ba6852c18a79af1d5b52ecee68007a6` | src/formats; portable parser/profile core |
| `src/formats/mz_tape_profiles.h` | 75 | `d1f179242e897c465bcf9eaafeaa5f1843a781b5e0988bb3099f80c742aff0ae` | src/formats; portable parser/profile core |
| `src/formats/mz_title.cpp` | 161 | `ba474c67ca15a63aa32752187aea20fbdd04ec966ed6fad32d66275a9d521429` | src/formats; portable parser/profile core |
| `src/formats/mz_title.h` | 20 | `0baf7ac452059fd7f095f4fb58972a4a61732dd4207fa29faf4e7ad36febe350` | src/formats; portable parser/profile core |
| `src/formats/wav_reader.cpp` | 480 | `cf520404e63d8e1017263faae90178593b2d5b83761cfcf47c69a1e275a18e01` | src/formats; portable parser/profile core |
| `src/formats/wav_reader.h` | 69 | `75d031ba68930900b7d886eb8f4abd411baa312e520d62553277c718c09f1b87` | src/formats; portable parser/profile core |
| `src/main.cpp` | 486 | `0509f52358e08c2775f66e5dfc2e41d9b4364de22077e28490cb8cd86c26952b` | Review dependency and license before port |
| `src/play/edge_playback.cpp` | 580 | `72dfb5b84d9ad17ca465cc5f8bec3b2f2b95cf9c7230d41def459419e7b9e9f3` | src/play + audio; port protocol, replace AVR timers |
| `src/play/edge_playback.h` | 37 | `cdd3ba1e1cc1b99073861884e790fb7561de0afbab6428aa75d63e04fb234147` | src/play + audio; port protocol, replace AVR timers |
| `src/play/loader_mode.h` | 30 | `c041eb663e12fc3d466c163c56e96dacc65bfd4e09bd1d8b558b17df78318c93` | src/play + audio; port protocol, replace AVR timers |
| `src/play/mz700_fast3.cpp` | 310 | `6434be240fc0d77e80841b06e36ee7c2bfbba275d5f604b10b8d7eb6415fc305` | src/play + audio; port protocol, replace AVR timers |
| `src/play/mz700_fast3.h` | 35 | `cf15097c88a0f2d5ad9ef4c6952d9ab07bef6a63ce38706fdc0b57c04f4e1708` | src/play + audio; port protocol, replace AVR timers |
| `src/play/mzf_loader.cpp` | 1233 | `72e825965218c9b2bff937dff140001d3395ff411beadaf5d6103eaa4903a208` | src/play + audio; port protocol, replace AVR timers |
| `src/play/mzf_loader.h` | 87 | `da3867c7b56ce667f5c2452cfcc90fde1a838e186863c96f2e157b15e8da239f` | src/play + audio; port protocol, replace AVR timers |
| `src/play/mzf_playback.cpp` | 3531 | `f63fdd1f4662160673ecebd26399be7c31d70349a6f6573a02324acb08d3a4e9` | src/play + audio; port protocol, replace AVR timers |
| `src/play/mzf_playback.h` | 89 | `496b0755c34d053a52550d1f482782cfc07e3a93491222d93716bccf02c410e4` | src/play + audio; port protocol, replace AVR timers |
| `src/play/play_controller.cpp` | 739 | `8b28efa3911415663c2aabb7810d6abd5a5498a7378cd44d427d0beb766d7193` | src/play + audio; port protocol, replace AVR timers |
| `src/play/play_controller.h` | 83 | `fd2835ecbc5ae9fe1596323e60a9c86872a0309509442643e0d89179666cb3aa` | src/play + audio; port protocol, replace AVR timers |
| `src/play/play_engine.cpp` | 839 | `6346bafb3c3979eada62024de7ecc0dced454e60f6b9e3a93ebc528b5b8a60f8` | src/play + audio; port protocol, replace AVR timers |
| `src/play/play_engine.h` | 86 | `7af934b338bb931cec7a1fede9ff9529613eb27cec0a18b91b1076ef1a019776` | src/play + audio; port protocol, replace AVR timers |
| `src/play/tap_playback.cpp` | 949 | `b9aec110100d84f58801735320dde45da1e903e7a8ec827baf52bb664e9c2e18` | src/play + audio; port protocol, replace AVR timers |
| `src/play/tap_playback.h` | 38 | `57ba980efd5870297ac40a4e8e95577ac1eb7e7cc035e66e31c743435f95e9eb` | src/play + audio; port protocol, replace AVR timers |
| `src/play/tap_speed.cpp` | 60 | `1ee60b56a9a81ad4e16b2ba46ba084c1a079cf6a33c4bc7642c68f635fad9c31` | src/play + audio; port protocol, replace AVR timers |
| `src/play/tap_speed.h` | 32 | `60c9ca7d584d663ddb256b54fa31c922981ad60882482aa650fa13a7ebee4359` | src/play + audio; port protocol, replace AVR timers |
| `src/play/timer3b_owner.cpp` | 4 | `87a794f093e01801d1af83ea4fbd689e05887a53387c3edfb3212c39b137a876` | src/play + audio; port protocol, replace AVR timers |
| `src/play/timer3b_owner.h` | 27 | `9f8ee25628700eab0852f033b542270e8489f05d27f7dcc44a944805cc9dac19` | src/play + audio; port protocol, replace AVR timers |
| `src/record/edge_record_driver.cpp` | 739 | `44de05f44a017dc19e98eb9a8d17f3d5d82655f743c26a2a100e9229f4ec7c55` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/edge_record_driver.h` | 58 | `c8e92ba558ff74d5ff8abfe4a1b66c2fbf2de2cac6b5e3f37103069c2ed5cf76` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/edge_record_engine.cpp` | 834 | `880355e3a5b2d9e51dc706212bc214f3b01be9bd1c089c2536a9e6e04c1780d5` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/edge_record_engine.h` | 37 | `a8c06464242307d7f6984c089ef89e0349d07c3cd48239b318b88b25864440b8` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/mzf_record_engine.cpp` | 656 | `87a75e3b022ddbb78fe9723985e9a7cd62cad936f81e5ef60105cedf2b086c09` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/mzf_record_engine.h` | 35 | `be5103c784102a094e2646aaad534a1cc21f1335dbc14b3fae92a7d698a936ca` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/record_autoname.cpp` | 1361 | `bc144f2718cbdcaced37a0dc57b7b3bba338e5064dc5c54920b6004b5458c977` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/record_autoname.h` | 59 | `f565abd50f9b7368f28358e86a8740b1bbbb4a359c1fa5ee5bfe3d29f96e07e1` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/record_engine.cpp` | 656 | `d6ee2b67d833763118a9bbcc888e234b01dfcfe64252a3c2790afe316c8cb495` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/record_engine.h` | 60 | `683cf1d4d867aed09445c5a66c1e370217929ee03795afacd150e589ae6625c3` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/record_path_buffer.cpp` | 13 | `455133a1d074a95fe10df645cda11101d4322d5355decc4a43de0dacffa35cf0` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/record_path_buffer.h` | 15 | `82803320706a12e28d43c2a501e3cecc470de194c9b3d210545e125a8dc06ea1` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/wav_record_engine.cpp` | 842 | `ae2e7724a1a62856018da40a30e04e096d87d3d0fe63d43a3d71ea458aa92f3a` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/record/wav_record_engine.h` | 61 | `f8e7fada6b8ecaf04c4bdbe932b70655c3c5c3fe0da6000ba81dbeb39306045b` | src/record + audio/recorder; port codec, replace ISR/FS ownership |
| `src/streams/cmt_error_buffers.cpp` | 5 | `9b59bb20c08d2afef0181322955cf68d96029921ca6fb37d64cf404ad6e4c400` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/cmt_error_buffers.h` | 11 | `52d7f910f08c824f642f8700a62a2a7613d558561ac505f4193b4d420f425c70` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/cmt_mode_scratch.cpp` | 4 | `44ed4e817926295d5c3d5de6d85fb3b09b7a338e16936ad171487f22c8894f33` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/cmt_mode_scratch.h` | 16 | `57b1c36fa1569c3dc864a82082c4fec5a8b38962c0a55aeb2a688b455d1c782d` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/cmt_sample_storage.cpp` | 4 | `eb6639ae4b81fa6f3f54d952727d55f1760262d144a2c874edb0c3e16a416854` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/cmt_sample_storage.h` | 14 | `779b332b32d45fb0cc82e84bb94234c72499ab42f82eed3156d44a346f37b9a3` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/cmt_work_buffer.cpp` | 4 | `e1b9bbe20080c497aba11c4f21a3eed40a191383f4c74e793ad34edb181927be` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/cmt_work_buffer.h` | 13 | `2e53ce4572a5160a45fcf7619635d622be9944738d0ff09a2f0655fb5232e8a0` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/record_sample_stream.cpp` | 115 | `5644c65d9438812502b8eceb12090eea738e4b3ee657225805db58ef13106859` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/record_sample_stream.h` | 39 | `3e396ea5e425b268d36abe731cc6c34ddb58aaa3f7060dd2320c99f8ee372830` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/wav_sample_stream.cpp` | 360 | `f633fdf6843e3862ebc28f2be07b1c55be9ce39dbd1e5cedcb1f39d0a7835cd7` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/streams/wav_sample_stream.h` | 128 | `dbe3de0b14ec251ddef3525edc7a9611341f9ba713fb1051c69f7194f5458ab9` | src/audio + bounded session buffers; replace AVR memory handling |
| `src/ui/browser.cpp` | 1116 | `bc72e8e4d23c1ab28b9eec97cb36c20110f97729c385469ff99bd29962506012` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/browser.h` | 35 | `6696fe2cd68ab07329774ba12fb96fc30bc7fc5aa518c4cb90b21b359d73dd03` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/menu.cpp` | 571 | `d8663d755720c4b2f2acff3ee6a2d1217b74f43fc2aed83df45d013dc8eac403` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/menu.h` | 32 | `d642af7aacee556e576db8eeb670aec220b1ee5a36cafff17e564ed34fad12fd` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/play_screen.cpp` | 520 | `a784ee991de68f9ad315b348cfa070ac161231af9db3a748a9dd22fad5ff8227` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/play_screen.h` | 18 | `25810c70a6002afe2e789c25cd54cf259dbead2d161d182ed8cb3050f1b02c65` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/record_menu.cpp` | 258 | `c23b6993de6d3ae14fcd1f85a722ef92946a4f4c11288affe5b1e134862a9c51` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/record_menu.h` | 35 | `21fa1ef91c8a688ccc42f6e78c9f9c09f1f6fce645543aa21ab6935210431895` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/record_screen.cpp` | 553 | `a97dcb7bd4d575182e437406c0e2a70915706cccbe4e269bda7c3c6dfc7bcf30` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/record_screen.h` | 19 | `f5267a921f8e2721be2328b2a8bbdd96acf63de1855c80e9aed8f0fa55c52c21` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/settings_menu.cpp` | 219 | `77ec2dbb345318214300f389bab98d1d631d45b9a0507dd277b7c0347723f82f` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/ui/settings_menu.h` | 20 | `f7fa2f06d6b7d017e8f46bbf0783dab05910f8af75b2474fd1b0b8a4c83eb48f` | src/ui + extracted app state; replace AVR LCD/keypad |
| `src/version.h` | 9 | `cd073e36c7818d44577fd5511e87b296a9591f8950f63a1c2f7d79f12bed6fbc` | Review dependency and license before port |

## Target files

| Target file | Lines | Disposition |
|---|---:|---|
| `src/audio/edge_capture.c` | 315 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/edge_capture.h` | 34 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/edge_capture_rmt.c` | 66 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/edge_capture_rmt.h` | 30 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/edge_stream.c` | 296 | Retain provisionally; signed interval/extension regression coverage |
| `src/audio/edge_stream.h` | 31 | Retain provisionally; signed interval/extension regression coverage |
| `src/audio/edge_writer.c` | 306 | Retain provisionally; signed interval/extension regression coverage |
| `src/audio/edge_writer.h` | 20 | Retain provisionally; signed interval/extension regression coverage |
| `src/audio/mzf_stream.c` | 1197 | Replace fixed timing and protocol preparation from authoritative reference |
| `src/audio/mzf_stream.h` | 38 | Replace fixed timing and protocol preparation from authoritative reference |
| `src/audio/player.c` | 1068 | Review integration against parity matrix |
| `src/audio/player.h` | 60 | Review integration against parity matrix |
| `src/audio/raw_capture.c` | 361 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/raw_capture.h` | 29 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/raw_out.c` | 81 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/raw_out.h` | 33 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/raw_pulse.c` | 530 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/raw_pulse.h` | 57 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/raw_timer.c` | 395 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/raw_timer.h` | 32 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/audio/recorder.c` | 691 | Retain during staged port; independent quantization fixed and host tested |
| `src/audio/recorder.h` | 51 | Retain during staged port; independent quantization fixed and host tested |
| `src/audio/wav_reader.c` | 272 | Retain provisionally; validate PCM/RIFF errors and limits |
| `src/audio/wav_reader.h` | 57 | Retain provisionally; validate PCM/RIFF errors and limits |
| `src/audio/wav_writer.c` | 192 | Retain provisionally; validate PCM/RIFF errors and limits |
| `src/audio/wav_writer.h` | 24 | Retain provisionally; validate PCM/RIFF errors and limits |
| `src/drivers/buttons.c` | 172 | Retain pin assignments; verify debounce, repeat and STOP priority |
| `src/drivers/buttons.h` | 28 | Retain pin assignments; verify debounce, repeat and STOP priority |
| `src/drivers/font6x8.h` | 98 | Retain SSD1306 primitives; add measured refresh scheduling |
| `src/drivers/i2c.c` | 82 | Retain SSD1306 primitives; add measured refresh scheduling |
| `src/drivers/i2c.h` | 9 | Retain SSD1306 primitives; add measured refresh scheduling |
| `src/drivers/oled.c` | 107 | Retain SSD1306 primitives; add measured refresh scheduling |
| `src/drivers/oled.h` | 10 | Retain SSD1306 primitives; add measured refresh scheduling |
| `src/drivers/sdcard.c` | 140 | Retain SPI2 DMA setup, redesign ownership/recovery |
| `src/drivers/sdcard.h` | 11 | Retain SPI2 DMA setup, redesign ownership/recovery |
| `src/file_list.c` | 727 | Replace allocation per entry and blocking full directory scan |
| `src/file_list.h` | 44 | Replace allocation per entry and blocking full directory scan |
| `src/gpio_raw.c` | 47 | Review integration against parity matrix |
| `src/gpio_raw.h` | 18 | Review integration against parity matrix |
| `src/main.c` | 1720 | Split UI, SD lifecycle, transport and settings; retain behavior until replacements are tested |
| `src/raw_stream.c` | 32 | Replace or consolidate timing backend; keep explicit diagnostic fallback |
| `src/ui/ui.c` | 77 | Review integration against parity matrix |
| `src/ui/ui.h` | 11 | Review integration against parity matrix |
| `src/wav.h` | 16 | Review integration against parity matrix |

## Subsequent TAP implementation (uncommitted working changes)

The 98-file reference inventory above remains the initial snapshot. New target modules audio/tap_stream.c/.h port the TAP state sequence and four timing profiles from play/tap_playback.cpp and tap_speed.cpp, with GPLv3 attribution. Integration changes affect player.c/.h, raw_pulse.c/.h, file_list.c, main.c and the component source list. The existing pulse FIFO is reused. Host tests link the actual source and driver translation units, see [current verification](TEST_REPORT.md). No branch, commit or push was created for this continuation.
