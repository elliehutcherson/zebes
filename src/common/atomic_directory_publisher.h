#pragma once

#include <filesystem>
#include <string>

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"

namespace zebes {

using AtomicDirectoryWriter =
    absl::AnyInvocable<absl::Status(const std::filesystem::path& staging_directory)>;

// Checks the immutable part of the publication contract without writing: the
// path must name a directory that does not exist. Call this before an external
// state change when publication failure would otherwise be discovered too
// late. PublishNewDirectoryAtomically repeats the check to close ordinary
// caller races as far as the filesystem API permits.
absl::Status ValidateNewDirectoryDestination(const std::string& output_path);

// Invokes writer in a private sibling staging directory, then renames the
// complete directory into place. Existing output is never replaced. The
// staging directory is removed on any writer or rename failure.
absl::Status PublishNewDirectoryAtomically(const std::string& output_path,
                                           AtomicDirectoryWriter writer);

}  // namespace zebes
