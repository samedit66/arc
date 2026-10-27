note
	description: "Sequential libarchive reader with optional block-oriented file and directory extraction."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_READER

create
	make_open_read,
	make_open_read_with_password

feature {NONE} -- Initialization

	make_open_read (path: PATH)
			-- Open one archive without a password and position at its first entry.
		require
			valid_path: {ARC_PATHS}.is_valid_path (path)
		do
			initialize (path, Void)
		ensure
			opened_or_failed: is_open or has_failed
			no_extraction: not is_extracting
		end

	make_open_read_with_password (path: PATH; password: READABLE_STRING_32)
			-- Open one archive using a UTF-8 password; do not retain the caller's string.
		require
			valid_path: {ARC_PATHS}.is_valid_path (path)
			valid_password: not password.is_empty and not password.has ('%U')
		do
			initialize (path, password)
		ensure
			opened_or_failed: is_open or has_failed
			no_extraction: not is_extracting
		end

	initialize (path: PATH; password: detachable READABLE_STRING_32)
			-- Initialize owned state and open input with an optional password.
		do
			create native.make
			create entry_slot.make ({PLATFORM}.pointer_bytes)
			create byte_count.make ({PLATFORM}.natural_64_bytes)
			create name_snapshot.make_empty
			create filter_snapshots.make (0)
			encryption_status := Encryption_unknown

			open_archive (path, password)
		end

feature -- Reading

	forth
			-- Advance the current header, skipping unread content and invalidating its entry.
		require
			positioned: is_open and has_entry and not has_failed
			no_pending_extraction: not is_extracting
		do
			read_header
		rescue
			close
		end

	consume_current
			-- Read the remaining current body to EOF without extracting or advancing it.
		require
			positioned: is_open and has_entry and not has_failed
			idle: not is_extracting
		local
			complete: BOOLEAN
		do
			from until complete or has_failed loop
				native.consume_block (handle)
				if native.last_status = native.Archive_eof then
					complete := True
				else
					check_status (handle, native.last_status)
				end
			end
			capture_summary
		ensure
			metadata_retained: has_entry
			name_preserved: entry_name = old entry_name
		rescue
			close
		end

feature -- Archive metadata

	format_name: detachable IMMUTABLE_STRING_32
			-- Detected format snapshot, retained after closing; Void when unavailable.

	filter_count: INTEGER
			-- Number of filter name snapshots, including libarchive's terminal none filter.
		do
			Result := filter_snapshots.count
		end

	filter_name (index: INTEGER): IMMUTABLE_STRING_32
			-- Retainable filter name at a one-based index.
		require
			valid_index: index >= 1 and index <= filter_count
		do
			Result := filter_snapshots [index]
		end

	encryption_status: INTEGER
			-- Detection result for data inspected so far, retained after closing.

	Encryption_detected: INTEGER = 1
			-- Native status indicating an encrypted entry was detected.

	Encryption_none_detected: INTEGER = 0
			-- Native status indicating no encrypted entry has been detected so far.

	Encryption_unknown: INTEGER = -1
			-- Native status indicating insufficient information.

	Encryption_unsupported: INTEGER = -2
			-- Native status indicating encryption detection is unsupported.

	entry_is_hardlink: BOOLEAN
			-- Does the current entry declare a hardlink target?
		require
			positioned: has_entry
		do
			Result := hardlink_snapshot
		end

feature -- Extraction

	begin_extraction (destination_path: PATH)
			-- Restore the current file or directory at a caller-selected native path.
			-- Clone metadata to preserve the original name; preserve symlinks and reject hardlinks.
		require
			positioned: is_open and has_entry and not has_failed
			idle: not is_extracting
			valid_path: {ARC_PATHS}.is_valid_path (destination_path)
		local
			path: NATIVE_STRING
		do
			last_written_bytes := 0
			if (entry_type /= native.Regular_type and entry_type /= native.Directory_type and
				entry_type /= native.Symbolic_link_type) or native.has_hardlink (entry)
			then
				record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0, "Only files, directories and symbolic links can be extracted"))
			else
				if disk = default_pointer then
					disk := native.new_disk_writer
					native.configure_extraction (disk)
					check_status (disk, native.last_status)
				end

				if not has_failed then
					output_entry := native.cloned_entry (entry)
					path := destination_path.native_string
					native.entry_set_native_name (output_entry, path.item)
					native.write_header (disk, output_entry)
					check_status (disk, native.last_status)
					native.entry_free (output_entry)
					output_entry := default_pointer
					is_extracting := not has_failed
					is_entry_complete := False
					last_written_bytes := 0
				end
			end
		ensure
			metadata_retained: has_entry
			name_preserved: entry_name = old entry_name
			started_or_failed: is_extracting or has_failed
			no_bytes_yet: last_written_bytes = 0

		rescue
			close
		end

	extract_block
			-- Transfer one block; publish written bytes or mark successful entry EOF.
		require
			active: is_extracting and not is_entry_complete and not has_failed
		local
			status: INTEGER
		do
			last_written_bytes := 0
			native.extract_block (handle, disk, byte_count.item)
			status := native.last_status
			if status = native.Archive_eof then
				is_entry_complete := True
			else
				check_status (handle, status)
				if not has_failed then
					last_written_bytes := byte_count.read_natural_64 (0)
				end
			end
		ensure
			metadata_retained: has_entry
			failed_block_not_counted: has_failed implies last_written_bytes = 0
			eof_not_counted: is_entry_complete implies last_written_bytes = 0

		rescue
			close
		end

	finish_extraction
			-- Finish a complete disk entry while retaining the reader's current metadata.
		require
			complete: is_extracting and is_entry_complete and not has_failed
		do
			native.finish_entry (disk)
			check_status (disk, native.last_status)
			is_extracting := False
			is_entry_complete := False
		ensure
			entry_finished: not is_extracting
			metadata_retained: has_entry
			name_preserved: entry_name = old entry_name

		rescue
			close
		end

feature -- Closing

	close
			-- Release the extraction writer before the reader; preserve cleanup errors.
			-- Closing an unfinished extraction records failure; repeated calls are harmless.
		local
			status: INTEGER
		do
			if is_extracting and not has_failed then
				record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0, "Extraction was not finished"))
			end
			if output_entry /= default_pointer then
				native.entry_free (output_entry)
				output_entry := default_pointer
			end
			if disk /= default_pointer then
				native.close_writer (disk)
				check_status (disk, native.last_status)
				native.free_writer (disk)
				status := native.last_status
				disk := default_pointer
				check_release_status (status, "Cannot free extraction writer")
			end
			if handle /= default_pointer then
				native.close_reader (handle)
				check_status (handle, native.last_status)
				native.free_reader (handle)
				status := native.last_status
				handle := default_pointer
				check_release_status (status, "Cannot free archive reader")
			end
			entry := default_pointer
			is_open := False
			has_entry := False
			is_extracting := False
			is_entry_complete := False
		ensure
			closed: not is_open and not has_entry and not is_extracting
			failure_preserved: old has_failed implies has_failed
		end

feature -- Reader state

	is_open: BOOLEAN
			-- Does this object own an open reader, including at normal archive EOF?

	has_entry: BOOLEAN
			-- Is current-entry metadata available?

	after: BOOLEAN
			-- Is there no current entry because reading is unopened, ended, failed or closed?
		do
			Result := not has_entry
		end

feature -- Current entry

	entry_name: IMMUTABLE_STRING_32
			-- Copied current Unicode name, retainable after advancing or closing.
		require
			positioned: has_entry
		do
			Result := name_snapshot
		end

	entry_type: INTEGER
			-- Native AE_IF* entry type, without an additional wrapper enumeration.
		require
			positioned: has_entry
		do
			Result := type_snapshot
		end

	has_entry_size: BOOLEAN
			-- Does the current entry declare an uncompressed size?
		require
			positioned: has_entry
		do
			Result := size_available_snapshot
		end

	entry_size: INTEGER_64
			-- Declared logical uncompressed size, including sparse-file holes.
		require
			available: has_entry and then has_entry_size
		do
			Result := size_snapshot
		end

	has_link_target: BOOLEAN
			-- Does the current symbolic-link entry have a copied target?
		require
			positioned: has_entry
		do
			Result := link_snapshot /= Void
		end

	entry_link_target: IMMUTABLE_STRING_32
			-- Copied symbolic-link target; never resolved or normalized.
		require
			available: has_entry and then has_link_target
		do
			check attached link_snapshot as target then
				Result := target
			end
		end

feature -- Extraction state

	is_extracting: BOOLEAN
			-- Is a disk entry awaiting extraction finalization?

	is_entry_complete: BOOLEAN
			-- Has the active extraction reached entry EOF without failure?

	last_written_bytes: NATURAL_64
			-- Bytes written by the latest block command, excluding sparse-file holes.

feature -- Errors

	has_failed: BOOLEAN
			-- Has an expected operation or cleanup failure been recorded?
		do
			Result := failure /= Void
		end

	last_error: ARC_ERROR
			-- Immutable primary diagnostic with separate subsequent cleanup failures.
		require
			failed: has_failed
		do
			check attached failure as value then
				Result := value
			end
		end

feature {NONE} -- Implementation

	open_archive (path: PATH; password: detachable READABLE_STRING_32)
			-- Acquire a reader only after all attached Eiffel fields are initialized.
		local
			name: NATIVE_STRING
			bytes: C_STRING
			converter: UTF_CONVERTER
		do
			handle := native.new_reader
			if attached password as secret then
				create bytes.make (converter.utf_32_string_to_utf_8_string_8 (secret))
				native.add_read_password (handle, bytes.item)
				check_status (handle, native.last_status)
			end
			name := path.native_string
			if not has_failed then
				native.open_reader (handle, name.item)
				check_status (handle, native.last_status)
			end
			if not has_failed then
				is_open := True
				read_header
			else
				close
			end
		rescue
			close
		end

	read_header
			-- Advance the native stream and publish only completely captured metadata.
		local
			status: INTEGER
		do
			has_entry := False
			entry := default_pointer
			link_snapshot := Void
			native.read_header (handle, entry_slot.item)
			status := native.last_status

			if status /= native.Archive_eof then
				check_status (handle, status)
				if not has_failed then
					entry := entry_slot.read_pointer (0)
					capture_entry_metadata
				end
			end

			capture_summary

			if not has_entry then
				entry := default_pointer
			end
		end

	capture_entry_metadata
			-- Copy native entry values without retaining borrowed string storage.
		require
			borrowed_header: entry /= default_pointer
		local
			name_pointer: POINTER
			bytes: STRING_8
			converter: UTF_CONVERTER
		do
			native.read_entry_name (handle, entry)
			name_pointer := native.last_pointer
			if name_pointer = default_pointer then
				if native.error_message (handle) /= default_pointer then
					check_status (handle, native.last_status)
				else
					record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0,
						"Entry name is not representable as UTF-8"))
				end
			else
				create bytes.make_from_c (name_pointer)
				if converter.is_valid_utf_8_string_8 (bytes) then
					create name_snapshot.make_from_string (converter.utf_8_string_8_to_string_32 (bytes))
					type_snapshot := native.entry_type (entry)
					hardlink_snapshot := native.has_hardlink (entry)
					size_available_snapshot := native.has_entry_size (entry)
					if size_available_snapshot then
						size_snapshot := native.entry_size (entry)
					end
					has_entry := True
					if type_snapshot = native.Symbolic_link_type then
						capture_link_target
					end
				else
					record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0,
						"Entry name contains invalid UTF-8 bytes"))
				end
			end
		end

	capture_link_target
			-- Copy the current symbolic-link target through the explicit UTF-8 accessor.
		local
			bytes: STRING_8
			converter: UTF_CONVERTER
		do
			native.read_link_target (handle, entry)
			if native.last_pointer = default_pointer then
				record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0, "Cannot represent symbolic-link target"))
			else
				create bytes.make_from_c (native.last_pointer)
				if converter.is_valid_utf_8_string_8 (bytes) then
					create link_snapshot.make_from_string (converter.utf_8_string_8_to_string_32 (bytes))
				else
					record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0, "Invalid UTF-8 symbolic-link target"))
				end
			end
			if has_failed then
				has_entry := False
			end
		end

	capture_summary
			-- Snapshot available archive metadata without changing the stream position.
		local
			pointer: POINTER
			index: INTEGER
			bytes: STRING_8
			converter: UTF_CONVERTER
		do
			pointer := native.format_name (handle)
			if pointer /= default_pointer then
				create bytes.make_from_c (pointer)
				create format_name.make_from_string (converter.utf_8_string_8_to_string_32 (bytes))
			end
			filter_snapshots.wipe_out
			from index := 0 until index >= native.filter_count (handle) loop
				pointer := native.filter_name (handle, index)
				if pointer /= default_pointer then
					create bytes.make_from_c (pointer)
					filter_snapshots.extend (create {IMMUTABLE_STRING_32}.make_from_string (converter.utf_8_string_8_to_string_32 (bytes)))
				end
				index := index + 1
			end
			encryption_status := native.encryption_status (handle)
		end

	filter_snapshots: ARRAYED_LIST [IMMUTABLE_STRING_32]
			-- Owned filter names from the last stream command.

	hardlink_snapshot: BOOLEAN
			-- Current entry hardlink marker.

	check_status (owner: POINTER; status: INTEGER)
			-- Snapshot a failed native result before another call changes its diagnostic.
		do
			if status /= native.Archive_ok then
				record_failure (create {ARC_ERROR}.make_native (owner, status))
			end
		end

	check_release_status (status: INTEGER; message: STRING_8)
			-- Record a release failure after its consumed handle becomes unavailable.
		do
			if status /= native.Archive_ok then
				record_failure (create {ARC_ERROR}.make (status, 0, message))
			end
		end

	record_failure (value: ARC_ERROR)
			-- Keep the primary immutable failure and append subsequent cleanup diagnostics.
		do
			if attached failure as first then
				failure := first.with_cleanup_error (value)
			else
				failure := value
			end
		end

	native: ARC_NATIVE
			-- Native bindings with per-operation result slots.

	entry_slot: MANAGED_POINTER
			-- Output slot for a borrowed archive_entry pointer.

	byte_count: MANAGED_POINTER
			-- Output slot for a uint64_t successful-byte count.

	handle: POINTER
			-- Owned archive reader, retained at normal archive EOF.

	disk: POINTER
			-- Owned disk writer, acquired only when extraction begins.

	entry: POINTER
			-- Current entry borrowed from the reader; never freed independently.

	output_entry: POINTER
			-- Temporary owned metadata clone, released even on an Eiffel exception.

	type_snapshot: INTEGER
			-- Copied native AE_IF* type of the current entry.

	size_available_snapshot: BOOLEAN
			-- Did the current header declare a logical size?

	size_snapshot: INTEGER_64
			-- Copied logical size, meaningful when its availability flag is true.

	link_snapshot: detachable IMMUTABLE_STRING_32
			-- Copied current symlink target, absent for other entry types.

	name_snapshot: IMMUTABLE_STRING_32
			-- Copied current entry name, replaced only during header advancement.

	failure: detachable ARC_ERROR
			-- Immutable primary and cleanup diagnostics, absent on success.

invariant
	reader_owned_when_open: is_open = (handle /= default_pointer)
	entry_is_borrowed: has_entry = (entry /= default_pointer)
	entry_requires_reader: has_entry implies is_open
	disk_requires_reader: disk /= default_pointer implies is_open
	extraction_requires_entry: is_extracting implies has_entry
	extraction_owns_disk: is_extracting implies disk /= default_pointer
	complete_requires_extraction: is_entry_complete implies is_extracting
	no_published_temporary: output_entry = default_pointer

end
