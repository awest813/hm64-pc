#include "recomp.h"
#include "librecomp/overlays.hpp"
#include "recomp_overlays.inl"

extern "C" void register_game_overlays() {
    // num_code_sections = actual entries in section_table[] (not total ELF sections).
    // total_num_sections = total section count in the ELF, used to size section_addresses[].
    static constexpr size_t num_code_section_entries =
        sizeof(section_table) / sizeof(section_table[0]);

    recomp::overlays::overlay_section_table_data_t sections = {
        section_table,
        num_code_section_entries,
        num_sections
    };

    recomp::overlays::overlays_by_index_t overlays = {
        overlay_sections_by_index,
        sizeof(overlay_sections_by_index) / sizeof(overlay_sections_by_index[0])
    };

    recomp::overlays::register_overlays(sections, overlays);
    // nuAuMgr is no longer stubbed – n_alAudioFrame stub in
    // rsp_audio_patches.cpp handles silence at the audio-frame level instead.
}
