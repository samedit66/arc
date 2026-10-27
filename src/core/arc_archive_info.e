note
	description: "Immutable statistics and archive metadata for one remaining-header inspection."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_ARCHIVE_INFO

create {ARC_ARCHIVE}
	make

feature {NONE} -- Initialization

	make (input: ARC_READER;
		counts: TUPLE [entries, files, directories, symlinks, hardlinks, others, declared_bytes, unknown_sizes: NATURAL_64])
			-- Capture statistics and retain immutable metadata from a completed header pass.
		do
			entry_count := counts.entries
			file_count := counts.files
			directory_count := counts.directories
			symbolic_link_count := counts.symlinks
			hardlink_count := counts.hardlinks
			other_count := counts.others
			declared_file_bytes := counts.declared_bytes
			unknown_size_count := counts.unknown_sizes
			format_name := input.format_name
			encryption_status := input.encryption_status
			create filters.make (input.filter_count)
			across 1 |..| input.filter_count as index loop
				filters.extend (input.filter_name (index))
			end
		end

feature -- Statistics

	entry_count: NATURAL_64
			-- Number of inspected entries.

	file_count: NATURAL_64
			-- Number of ordinary files, excluding hardlinks.

	directory_count: NATURAL_64
			-- Number of directory entries.

	symbolic_link_count: NATURAL_64
			-- Number of symbolic links.

	hardlink_count: NATURAL_64
			-- Number of hardlink entries.

	other_count: NATURAL_64
			-- Number of entries of other types.

	declared_file_bytes: NATURAL_64
			-- Sum of known nonnegative declared ordinary-file sizes.

	unknown_size_count: NATURAL_64
			-- Number of ordinary files without a known nonnegative size.

feature -- Archive metadata

	format_name: detachable IMMUTABLE_STRING_32
			-- Detected format, or Void when unavailable.

	encryption_status: INTEGER
			-- Encryption detection status after inspection; ARC_READER defines the native statuses.

	filter_count: INTEGER
			-- Number of retained filter names, including libarchive's terminal none filter.
		do
			Result := filters.count
		end

	filter_name (index: INTEGER): IMMUTABLE_STRING_32
			-- Retainable filter name at a one-based index.
		require
			valid_index: index >= 1 and index <= filter_count
		do
			Result := filters [index]
		end

feature {NONE} -- Storage

	filters: ARRAYED_LIST [IMMUTABLE_STRING_32]
			-- Owned filter collection; immutable strings may be shared with the reader.

end
