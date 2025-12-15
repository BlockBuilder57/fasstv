// Created by block on 2025-12-14.

#pragma once

#include <libfasstv/SSTV.hpp>

#include <vector>

namespace fasstv {

	class SSTVMetadata {
	public:
		struct PerModeMetadata {
			SSTV::Mode* mode{};
			float length_ms; // total length of mode
			float loop_length_ms;
			float scan_length_total_ms;
		};

		static SSTV::Mode* mode_longest;
		static SSTV::Mode* mode_shortest;

		static void BuildMetadata();
		static PerModeMetadata* GetModeMetadata(SSTV::Mode* mode);

	private:
		static float mode_longest_ms;
		static float mode_shortest_ms;

		static void ProcessMetadata(SSTV::Mode* mode);

		static std::vector<PerModeMetadata> per_mode_metadata;
	};

} // namespace fasstv
