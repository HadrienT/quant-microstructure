#ifndef QUANTMICROSTRUCTURE_CORE_BUILD_INFO_HPP
#define QUANTMICROSTRUCTURE_CORE_BUILD_INFO_HPP

#include <string_view>

namespace quantMicrostructure
{

    /// What a caller records next to a result to reproduce it.
    struct BuildInfo
    {
        /// Version of this library, "major.minor.patch".
        std::string_view version;
        /// Commit of quant-microstructure the library was built from: 40 hex
        /// digits, followed by "-dirty" if the tree had uncommitted changes, or
        /// "unknown" if the build had no git information.
        std::string_view git_sha;
    };

    const BuildInfo &build_info() noexcept;

} // namespace quantMicrostructure

#endif
