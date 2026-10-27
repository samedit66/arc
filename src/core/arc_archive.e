note
	description: "Single-use archive with registered packing sources and sequential high-level extraction."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_ARCHIVE

create
	make_open_read,
	make_open_write,
	make_open_read_with_password,
	make_open_write_with_password

feature {NONE} -- Initialization

	make_open_read (path: PATH)
			-- Open an archive and position at its first entry; retain expected opening errors.
		require
			valid_path: {ARC_PATHS}.is_valid_path (path)
		do
			create sources.make (0)
			create input_owner.make_open_read (path)
		ensure
			opened_or_failed: is_open_read or has_failed
		end

	make_open_write (path: PATH; format: READABLE_STRING_8)
			-- Exclusively create output now; defer source reading and packing until `pack'.
		require
			valid_path: {ARC_PATHS}.is_valid_path (path)
			known_format: {ARC_FORMATS}.is_supported_format (format)
		do
			create sources.make (0)
			create output_owner.make_open_write (path, format)
		ensure
			opened_or_failed: is_open_write or has_failed
			no_sources: source_count = 0
		end

	make_open_read_with_password (path: PATH; password: READABLE_STRING_32)
			-- Open input using a UTF-8 password before reading its first header.
		require
			valid_path: {ARC_PATHS}.is_valid_path (path)
			valid_password: not password.is_empty and not password.has ('%U')
		do
			create sources.make (0)
			create input_owner.make_open_read_with_password (path, password)
		ensure
			opened_or_failed: is_open_read or has_failed
		end

	make_open_write_with_password (path: PATH; format: READABLE_STRING_8; password: READABLE_STRING_32)
			-- Exclusively create AES-256 ZIP output; retain errors for other profiles.
		require
			valid_path: {ARC_PATHS}.is_valid_path (path)
			known_format: {ARC_FORMATS}.is_supported_format (format)
			valid_password: not password.is_empty and not password.has ('%U')
		do
			create sources.make (0)
			create output_owner.make_open_write_with_password (path, format, password)
		ensure
			opened_or_failed: is_open_write or has_failed
			no_sources: source_count = 0
		end

feature -- Source registration

	add_path (source: PATH; archive_name: READABLE_STRING_32)
			-- Register an absolute source path and copied archive name without inspecting the source.
			-- Read contents during `pack'; an empty name includes a real directory's contents only.
		require
			ready: is_open_write and not has_failed
			idle: not is_busy
			valid_source: {ARC_PATHS}.is_valid_path (source)
			name_without_nul: not archive_name.has ('%U')
		do
			sources.extend ([source.absolute_path, create {IMMUTABLE_STRING_32}.make_from_string (archive_name)])
		ensure
			registered: source_count = old source_count + 1
		end

	source_count: INTEGER
			-- Number of registered sources, retained after packing, closing or failure.
		do
			Result := sources.count
		end

feature -- Packing

	pack
			-- Write registered sources and close; remove incomplete output after failure.
			-- An empty source set creates an empty archive; observer exceptions abort and propagate.
		require
			ready: is_open_write and not has_failed
			idle: not is_busy
		local
			callback: like progress_observer
		do
			is_busy := True
			callback := progress_observer
			check attached output_owner as output then
				across sources as source loop
					if not output.has_failed then
						visit (output, source.path, source.name.to_string_32, callback)
					end
				end

				output.close
			end
			is_busy := False
		ensure
			closed: not is_open
			idle_again: not is_busy
			sources_retained: source_count = old source_count
		rescue
			is_busy := False
			abort
		end

feature -- Reading

	forth
			-- Advance to the next entry, skipping unread content; never rewind.
		require
			positioned: has_entry
			idle: not is_busy
		do
			check attached input_owner as input then
				input.forth
			end
		end

	has_entry: BOOLEAN
			-- Is current-entry metadata available without an operation failure?
		do
			Result := not has_failed and then attached input_owner as input and then input.has_entry
		end

	after: BOOLEAN
			-- Is no entry available because the archive ended, failed or was closed?
		do
			Result := not has_entry
		end

	entry_name: IMMUTABLE_STRING_32
			-- Copied current Unicode pathname, retainable after advancing or closing.
		require
			available: has_entry
		do
			check attached input_owner as input then
				Result := input.entry_name
			end
		end

	entry_type: INTEGER
			-- Current native AE_IF* entry type.
		require
			available: has_entry
		do
			check attached input_owner as input then
				Result := input.entry_type
			end
		end

	entry_is_regular: BOOLEAN
			-- Does the current entry have the native ordinary-file type?
		require
			available: has_entry
		do
			Result := entry_type = {ARC_NATIVE}.Regular_type
		end

	entry_is_directory: BOOLEAN
			-- Is the current entry a directory?
		require
			available: has_entry
		do
			Result := entry_type = {ARC_NATIVE}.Directory_type
		end

	entry_is_symbolic_link: BOOLEAN
			-- Is the current entry a symbolic link?
		require
			available: has_entry
		do
			Result := entry_type = {ARC_NATIVE}.Symbolic_link_type
		end

	has_entry_size: BOOLEAN
			-- Does the current entry declare an uncompressed size?
		require
			available: has_entry
		do
			check attached input_owner as input then
				Result := input.has_entry_size
			end
		end

	entry_size: INTEGER_64
			-- Declared logical uncompressed size.
		require
			available: has_entry and then has_entry_size
		do
			check attached input_owner as input then
				Result := input.entry_size
			end
		end

	has_link_target: BOOLEAN
			-- Does the current entry have a copied symbolic-link target?
		require
			available: has_entry
		do
			check attached input_owner as input then
				Result := input.has_link_target
			end
		end

	entry_link_target: IMMUTABLE_STRING_32
			-- Copied symbolic-link target, never resolved or normalized.
		require
			available: has_entry and then has_link_target
		do
			check attached input_owner as input then
				Result := input.entry_link_target
			end
		end

feature -- Inspection

	inspection: detachable ARC_ARCHIVE_INFO
			-- Last successful remaining-header summary, retained after closing; Void before inspection or on its failure.

	inspect_remaining
			-- Inspect headers from the current entry through EOF without verifying bodies or closing input.
			-- Replace `inspection' on success; leave it Void on failure. Do not extract files.
		require
			ready: is_open_read and not has_failed
			idle: not is_busy
		local
			entries, files, directories, symlinks, hardlinks, others, unknown_sizes: NATURAL_64
			declared_bytes: NATURAL_64
		do
			is_busy := True
			inspection := Void
			check attached input_owner as input then
				from until input.after or has_failed loop
					entries := entries + 1
					if input.entry_is_hardlink then
						hardlinks := hardlinks + 1
					elseif input.entry_type = {ARC_NATIVE}.Directory_type then
						directories := directories + 1
					elseif input.entry_type = {ARC_NATIVE}.Regular_type then
						files := files + 1
						if input.has_entry_size and then input.entry_size >= 0 then
							if declared_bytes > {NATURAL_64}.max_value - input.entry_size.to_natural_64 then
								failure := create {ARC_ERROR}.make ({ARC_NATIVE}.Archive_failed, 0, "Declared file byte total exceeds supported range")
							else
								declared_bytes := declared_bytes + input.entry_size.to_natural_64
							end
						else
							unknown_sizes := unknown_sizes + 1
						end
					elseif input.entry_type = {ARC_NATIVE}.Symbolic_link_type then
						symlinks := symlinks + 1
					else
						others := others + 1
					end
					if not has_failed then
						input.forth
					end
				end

				if not has_failed then
					create inspection.make (input, [entries, files, directories, symlinks, hardlinks, others, declared_bytes, unknown_sizes])
				end
			end
			is_busy := False
		ensure
			idle_again: not is_busy
			consumed_or_failed: after
			input_retained: is_open_read
			result_available: not has_failed implies inspection /= Void
			no_partial_result: has_failed implies inspection = Void
		rescue
			is_busy := False
			close
		end

	verify_remaining
			-- Consume all remaining bodies through EOF without extracting or closing input.
			-- Retain libarchive data errors; checks depend on the format's integrity support.
		require
			ready: is_open_read and not has_failed
			idle: not is_busy
		do
			is_busy := True
			check attached input_owner as input then
				from until input.after or input.has_failed loop
					input.consume_current
					if not input.has_failed then
						input.forth
					end
				end
			end
			is_busy := False
		ensure
			idle_again: not is_busy
			consumed_or_failed: after
			input_retained: is_open_read
		rescue
			is_busy := False
			close
		end

	is_readable: BOOLEAN
			-- Was this archive created for reading, including after closing?
		do
			Result := input_owner /= Void
		end

	consume_current
			-- Consume the current body without extracting or advancing it.
		require
			positioned: is_open_read and has_entry and not has_failed
			idle: not is_busy
		do
			check attached input_owner as input then
				input.consume_current
			end
		ensure
			metadata_retained: has_entry
			name_preserved: entry_name = old entry_name
		end

	format_name: detachable IMMUTABLE_STRING_32
			-- Detected format snapshot, retained after closing.
		require
			input_available: is_readable
		do
			check attached input_owner as input then
				Result := input.format_name
			end
		end

	filter_count: INTEGER
			-- Number of retained filter names.
		require
			input_available: is_readable
		do
			check attached input_owner as input then
				Result := input.filter_count
			end
		end

	filter_name (index: INTEGER): IMMUTABLE_STRING_32
			-- Retainable filter name at a one-based index.
		require
			valid_index: index >= 1 and index <= filter_count
		do
			check attached input_owner as input then
				Result := input.filter_name (index)
			end
		end

	encryption_status: INTEGER
			-- Encryption detection result for inspected data; ARC_READER defines the statuses.
		require
			input_available: is_readable
		do
			check attached input_owner as input then
				Result := input.encryption_status
			end
		end

	entry_is_hardlink: BOOLEAN
			-- Does the current entry declare a hardlink target?
		require
			positioned: has_entry
		do
			check attached input_owner as input then
				Result := input.entry_is_hardlink
			end
		end

feature -- Extraction

	extraction_directory: detachable PATH
			-- Physical root of the last successfully created `extract_all' destination, otherwise Void.

	extract_current (destination_path: PATH)
			-- Restore the current entry at an explicit path without advancing the reader.
			-- On observer exceptions close resources and propagate; restored objects may remain.
		require
			positioned: has_entry
			idle: not is_busy
			valid_path: {ARC_PATHS}.is_valid_path (destination_path)
		do
			is_busy := True
			check attached input_owner as input then
				extract_entry (input, destination_path, progress_observer)
			end
			is_busy := False
		ensure
			idle_again: not is_busy
		rescue
			is_busy := False
			close
		end

	extract_all (destination_directory: PATH)
			-- Create a new destination and extract from the current entry through EOF, keeping input open.
			-- Reject absolute/parent entry paths; never traverse restored symlinks.
			-- Preserve already restored objects on failure; observer exceptions close and propagate.
		require
			ready: is_open_read and not has_failed
			idle: not is_busy
			valid_path: {ARC_PATHS}.is_valid_path (destination_directory)
		local
			native: ARC_NATIVE
			name, resolved_name: NATIVE_STRING
			resolved: POINTER
			destination: PATH
			callback: like progress_observer
		do
			is_busy := True
			callback := progress_observer
			create native.make
			name := destination_directory.native_string
			native.create_directory (name.item)
			resolved := native.last_pointer
			if resolved = default_pointer then
				failure := create {ARC_ERROR}.make_system (native.last_system_code, "Cannot create extraction destination")
				if native.last_cleanup_code /= 0 then
					check attached failure as primary then
						failure := primary.with_cleanup_error (create {ARC_ERROR}.make_system (
							native.last_cleanup_code, "Cannot close destination handle"))
					end
				end
			else
				create resolved_name.make_from_pointer (resolved)
				create destination.make_from_string (resolved_name.string)
				extraction_directory := destination
				native.free_path (resolved)
				resolved := default_pointer

				check attached input_owner as input then
					from until after loop
						if not {ARC_PATHS}.is_valid_extraction_name (entry_name, {PLATFORM}.is_windows) then
							failure := create {ARC_ERROR}.make (native.Archive_failed, 0, "Invalid relative archive pathname")
						else
							extract_entry (input, destination.extended (entry_name), callback)
							if not has_failed then
								input.forth
							end
						end
					end
				end
			end
			is_busy := False
		ensure
			idle_again: not is_busy
			consumed_or_failed: after
		rescue
			if resolved /= default_pointer and attached native as active_native then
				active_native.free_path (resolved)
			end
			is_busy := False
			close
		end

feature -- Progress

	set_progress (callback: detachable PROCEDURE [TUPLE [name: IMMUTABLE_STRING_32; written, total: NATURAL_64]])
			-- Retain a synchronous per-file callback, or disable it with Void; disabled by default.
			-- Report successful bytes, with total zero if unknown; callbacks must not mutate this archive.
		require
			idle: not is_busy
		do
			progress_observer := callback
		end

feature -- Lifecycle

	close
			-- Close input, or cancel output unless `pack' already committed it; never start packing.
			-- Repeated calls are harmless; registered sources remain available through `source_count'.
		require
			idle: not is_busy
		do
			if attached input_owner as input then
				input.close
			elseif attached output_owner as output then
				output.abort
			end
		ensure
			closed: not is_open
		end

	abort
			-- Cancel output and remove it where possible, or close input; preserve committed output.
		require
			idle: not is_busy
		do
			close
		ensure
			closed: not is_open
		end

	is_open: BOOLEAN
			-- Does this archive own an open reader or writer?
		do
			Result := is_open_read or is_open_write
		end

	is_open_read: BOOLEAN
			-- Is this archive open for sequential reading?
		do
			Result := attached input_owner as input and then input.is_open
		end

	is_open_write: BOOLEAN
			-- Is this archive open for registered packing?
		do
			Result := attached output_owner as output and then output.is_open
		end

	is_aborted: BOOLEAN
			-- Was uncommitted output abandoned, including by `close' before `pack'?
		do
			Result := attached output_owner as output and then output.is_aborted
		end

	is_busy: BOOLEAN
			-- Is packing, extraction, inspection or verification active, including a progress callback?

feature -- Errors

	has_failed: BOOLEAN
			-- Has an expected operation or cleanup failure occurred?
		do
			Result := failure /= Void or else
				(attached input_owner as input and then input.has_failed) or else
				(attached output_owner as output and then output.has_failed)
		end

	last_error: ARC_ERROR
			-- Immutable primary diagnostic, including separately retained cleanup failures.
		require
			failed: has_failed
		do
			if attached failure as value then
				Result := value
				if attached input_owner as input and then input.has_failed then
					Result := value.with_cleanup_error (input.last_error)
				end
			elseif attached input_owner as input and then input.has_failed then
				Result := input.last_error
			else
				check attached output_owner as output then
					Result := output.last_error
				end
			end
		end

feature {NONE} -- Extraction implementation

	extract_entry (input: ARC_READER; destination: PATH; callback: like progress_observer)
			-- Complete one entry with successful-byte notifications and no cursor advancement.
		local
			written, total: NATURAL_64
		do
			if input.has_entry_size and then input.entry_size >= 0 then
				total := input.entry_size.to_natural_64
			end
			input.begin_extraction (destination)
			from until input.has_failed or else input.is_entry_complete loop
				input.extract_block
				if not input.has_failed and then input.last_written_bytes > 0 then
					written := written + input.last_written_bytes
					notify (callback, input.entry_name, written, total)
				end
			end
			if not input.has_failed then
				input.finish_extraction
			end
		end

feature {NONE} -- Ownership

	input_owner: detachable ARC_READER
			-- Exclusively owned input, absent for a write-mode archive.

	output_owner: detachable ARC_WRITER
			-- Exclusively owned output, absent for a read-mode archive.

	sources: ARRAYED_LIST [TUPLE [path: PATH; name: IMMUTABLE_STRING_32]]
			-- Owned source registrations with absolute paths and immutable archive names.

	progress_observer: detachable PROCEDURE [TUPLE [name: IMMUTABLE_STRING_32; written, total: NATURAL_64]]
			-- Optional callback captured before each operation's first notification.

	failure: detachable ARC_ERROR
			-- High-level pathname or destination failure, preceding input_owner cleanup errors.

feature {NONE} -- Traversal

	visit (writer: ARC_WRITER; path: PATH; name: STRING_32;
		progress: detachable PROCEDURE [TUPLE [name: IMMUTABLE_STRING_32; written, total: NATURAL_64]])
			-- Classify one source physically and dispatch without following links.
		local
			info: ARC_PATHS
			written: NATURAL_64
		do
			if not writer.has_failed and not path.same_as (writer.output_path) then
				create info
				info.classify_source (path)

				if info.source_error_code /= 0 then
					writer.report_system_failure (info.source_error_code, "Cannot inspect source")
					if info.source_cleanup_code /= 0 then
						writer.report_system_failure (info.source_cleanup_code, "Cannot close source metadata handle")
					end
				elseif info.is_symbolic_link then
					if name.is_empty then
						writer.report_failure ("An empty archive root requires a real directory")
					else
						writer.begin_symbolic_link (path, name)
						finish_if_successful (writer)
					end
				elseif info.is_directory then
					if not name.is_empty then
						writer.begin_directory (name)
						finish_if_successful (writer)
					end
					if not writer.has_failed then
						visit_directory (writer, path, name, progress)
					end
				elseif info.is_regular then
					writer.check_output_source (path)
					if writer.has_failed or writer.is_output_source then
						-- Exclude the owned output even when parent directories have path aliases.
					elseif name.is_empty then
						writer.report_failure ("An empty archive root requires a real directory")
					else
						writer.begin_file (path, name)
						from
						until
							writer.has_failed or else writer.is_entry_complete
						loop
							writer.write_block
							if not writer.has_failed and then writer.last_written_bytes > 0 then
								written := written + writer.last_written_bytes
								notify (progress, name, written, writer.current_size.to_natural_64)
							end
						end
						finish_if_successful (writer)
					end
				else
					writer.report_failure ("Only files, directories and symbolic links can be packed")
				end
			end
		end

	visit_directory (writer: ARC_WRITER; path: PATH; name: STRING_32;
		progress: detachable PROCEDURE [TUPLE [name: IMMUTABLE_STRING_32; written, total: NATURAL_64]])
			-- Enumerate one iterator in Eiffel using checked single-step native primitives.
		local
			native: ARC_NATIVE
			directory: POINTER
			native_path: NATIVE_STRING
			native_name: NATIVE_STRING
			child_name: STRING_32
			archive_name: STRING_32
		do
			create native.make
			native_path := path.native_string
			native.open_directory (native_path.item)
			directory := native.last_pointer
			check_directory_error (writer, native)

			if directory /= default_pointer then
				native.read_directory_entry (directory)
				check_directory_error (writer, native)
				from
				until
					writer.has_failed or native.last_pointer = default_pointer
				loop
					create native_name.make_from_pointer (native.last_pointer)
					child_name := native_name.string
					if not child_name.same_string (".") and not child_name.same_string ("..") then
						archive_name := child_name.twin
						if not name.is_empty then
							archive_name.prepend (name + "/")
						end
						visit (writer, path.extended (child_name), archive_name, progress)
					end
					if not writer.has_failed then
						native.read_directory_entry (directory)
						check_directory_error (writer, native)
					end
				end

				native.close_directory (directory)
				directory := default_pointer
				check_directory_error (writer, native)
			end
		rescue
			if directory /= default_pointer and attached native as active_native then
				active_native.close_directory (directory)
				directory := default_pointer
				check_directory_error (writer, active_native)
			end
		end

	check_directory_error (writer: ARC_WRITER; native: ARC_NATIVE)
			-- Snapshot one iterator's errno before another native call changes its result slot.
		do
			if native.last_system_code /= 0 then
				writer.report_system_failure (native.last_system_code, "Source directory I/O failed")
			end
		end

	finish_if_successful (writer: ARC_WRITER)
			-- Finish the current metadata or file entry only after successful preparation.
		do
			if not writer.has_failed then
				writer.finish_entry
			end
		end

	notify (progress: detachable PROCEDURE [TUPLE [name: IMMUTABLE_STRING_32; written, total: NATURAL_64]];
		name: STRING_32; written, total: NATURAL_64)
			-- Publish immutable progress arguments outside native calls; propagate observer exceptions.
		local
			snapshot: IMMUTABLE_STRING_32
		do
			if attached progress as callback then
				create snapshot.make_from_string (name)
				callback.call ([snapshot, written, total])
			end
		end


invariant
	one_mode: (input_owner /= Void) /= (output_owner /= Void)
	busy_requires_resource: is_busy implies is_open

end
