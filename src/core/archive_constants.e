note
	description: "Shared archive identifiers and native status values inherited by archive facilities."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_CONSTANTS

feature -- Error categories

	Open_error: INTEGER = unique
			-- Failure to open or configure the archive reader and writer.

	Destination_error: INTEGER = unique
			-- Failure to create or resolve the destination directory.

	Read_error: INTEGER = unique
			-- Failure to read an archive header or data block.

	Path_error: INTEGER = unique
			-- Invalid or unrepresentable entry pathname or hardlink target.

	Unsupported_entry_error: INTEGER = unique
			-- Archive entry type that extraction does not support.

	Write_error: INTEGER = unique
			-- Failure to write or finish an extracted entry.

	Cleanup_error_code: INTEGER = unique
			-- Failure while closing or releasing native resources.

feature -- Entry kinds

	Regular_file: INTEGER = unique
			-- Ordinary file.

	Directory: INTEGER = unique
			-- Directory.

	Symbolic_link: INTEGER = unique
			-- Symbolic link.

	Hard_link: INTEGER = unique
			-- Hardlink entry.

	Other: INTEGER = unique
			-- Other or unknown native entry type.

feature -- Encryption observations

	Encryption_unknown: INTEGER = unique
			-- Insufficient information to determine encryption.

	Encryption_unsupported: INTEGER = unique
			-- Encryption detection unavailable for the detected format.

	Encryption_detected: INTEGER = unique
			-- At least one encrypted entry detected.

	Encryption_none_detected: INTEGER = unique
			-- Detection supported, with no encrypted entry found so far.

feature -- Encoding identifiers

	Unknown_format: INTEGER = unique
			-- No recognized filename encoding.

	Zip: INTEGER = unique
			-- ZIP container.

	Tar: INTEGER = unique
			-- POSIX pax TAR container.

	Seven_zip: INTEGER = unique
			-- 7z container.

	None: INTEGER = unique
			-- No outer compression.

	Gzip: INTEGER = unique
			-- Gzip outer compression.

	Bzip2: INTEGER = unique
			-- Bzip2 outer compression.

	Xz: INTEGER = unique
			-- XZ outer compression.

	Zstd: INTEGER = unique
			-- Zstandard outer compression.

feature -- Native status values

	End_status: INTEGER = 1
			-- Normal native end-of-entry or end-of-archive result.

	Read_failure_status: INTEGER = -2
			-- Source read failure from the packing transfer primitive.

	Write_failure_status: INTEGER = -2
			-- Disk write failure from the extraction transfer primitive.

	Native_encryption_unsupported: INTEGER = -2
			-- ARCHIVE_READ_FORMAT_ENCRYPTION_UNSUPPORTED from libarchive.

end
