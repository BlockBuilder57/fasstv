// Created by block on 2025-12-14.

#pragma once

#include <libfasstv/SSTV.hpp>

#include <vector>

namespace fasstv {

	class SSTVMetadata {
	public:
		struct PerModeMetadata {
			SSTV::Mode* mode {};
			float length_ms {}; // total length of mode
			float loop_length_ms {}; // length of looping instructions
			float scan_total_length_ms {}; // summed time of all scans
			float sync_between_ms {}; // time between sync pulses
			float sync_length_ms {}; // length of sync pulse
			int newline_interval {};
		};

		static SSTV::Mode* mode_longest;
		static SSTV::Mode* mode_shortest;

		static SSTV::Mode* mode_shortest_sync;

		static void BuildMetadata();
		static PerModeMetadata* GetModeMetadata(SSTV::Mode* mode);

	private:
		static float mode_longest_ms;
		static float mode_shortest_ms;
		static float mode_shortest_sync_ms;

		static void ProcessMetadata(SSTV::Mode* mode);

		static std::vector<PerModeMetadata> per_mode_metadata;
	};

} // namespace fasstv
