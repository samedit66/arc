note
	description: "Block-oriented owner of a libarchive writer, one source FILE and one entry."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_WRITER

create
	make_open_write,
	make_open_write_with_password

feature {NONE} -- Initialization

	make_open_write (path: PATH; format: READABLE_STRING_8)
			-- Exclusively create one unencrypted archive using the selected profile.
		require
			valid_path: {ARC_PATHS}.is_valid_path (path)
			known_format: {ARC_FORMATS}.is_supported_format (format)
		do
			initialize (path, format, Void)
		ensure
			opened_or_failed: is_open or has_failed
			no_pending_entry: not has_entry
		end

	make_open_write_with_password (path: PATH; format: READABLE_STRING_8; password: READABLE_STRING_32)
			-- Create ZIP with AES-256; reject other profiles before creating output.
		require
			valid_path: {ARC_PATHS}.is_valid_path (path)
			known_format: {ARC_FORMATS}.is_supported_format (format)
			valid_password: not password.is_empty and not password.has ('%U')
		do
			initialize (path, format, password)
		ensure
			opened_or_failed: is_open or has_failed
			no_pending_entry: not has_entry
		end

	initialize (path: PATH; format: READABLE_STRING_8; password: detachable READABLE_STRING_32)
			-- Initialize owned state and configure output with an optional password.
		do
			output_descriptor := -1
			create native.make
			create byte_count.make ({PLATFORM}.natural_64_bytes)
			create cleanup_code.make ({PLATFORM}.integer_32_bytes)
			output_path := path.absolute_path

			open_archive (format, password)
		end

feature -- Entry creation

	begin_file (source_path: PATH; entry_name: READABLE_STRING_32)
			-- Open an ordinary source and write its header under a copied UTF-8 name.
		require
			ready: is_open and not has_failed and not has_entry
			valid_source: {ARC_PATHS}.is_valid_path (source_path)
			valid_name: {ARC_PATHS}.is_valid_entry_name (entry_name)
		local
			path: NATIVE_STRING
		do
			entry := native.new_entry
			has_entry := True
			is_entry_complete := False
			last_written_bytes := 0
			entry_written_bytes := 0
			path := source_path.native_string
			if {PLATFORM}.is_windows then
				prepare_metadata_reader
			end
			if not has_failed then
				native.open_source (handle, metadata_reader, entry, path.item, cleanup_code.item)
				source := native.last_pointer
				if source = default_pointer then
					check_status (native.Archive_failed)
					if cleanup_code.read_integer_32 (0) /= 0 then
						record_failure (create {ARC_ERROR}.make (native.Archive_failed,
							cleanup_code.read_integer_32 (0), "Cannot close source after metadata failure"))
					end
				else
					size_snapshot := native.entry_size (entry)
					set_entry_name (entry_name)
					native.write_header (handle, entry)
					check_status (native.last_status)
				end
			end
		ensure
			entry_owned: has_entry
			content_pending: not is_entry_complete
			no_bytes_yet: last_written_bytes = 0

		rescue
			abort
		end

	begin_directory (entry_name: READABLE_STRING_32)
			-- Write an empty directory header with mode 0755; do not traverse it.
		require
			ready: is_open and not has_failed and not has_entry
			valid_name: {ARC_PATHS}.is_valid_entry_name (entry_name)
		do
			entry := native.new_entry
			has_entry := True
			is_entry_complete := True
			size_snapshot := 0
			last_written_bytes := 0
			entry_written_bytes := 0
			native.entry_set_directory (entry)
			set_entry_name (entry_name)
			native.write_header (handle, entry)
			check_status (native.last_status)
		ensure
			entry_owned: has_entry
			no_content: current_size = 0
			ready_to_finish: not has_failed implies is_entry_complete

		rescue
			abort
		end

	begin_symbolic_link (source_path: PATH; entry_name: READABLE_STRING_32)
			-- Store the symbolic link itself, including a dangling target, without traversal.
		require
			ready: is_open and not has_failed and not has_entry
			valid_source: {ARC_PATHS}.is_valid_path (source_path)
			valid_name: {ARC_PATHS}.is_valid_entry_name (entry_name)
		local
			path: NATIVE_STRING
		do
			entry := native.new_entry
			has_entry := True
			is_entry_complete := True
			size_snapshot := 0
			last_written_bytes := 0
			entry_written_bytes := 0
			prepare_metadata_reader

			if not has_failed then
				path := source_path.native_string
				native.read_source_metadata (metadata_reader, entry, path.item)
				check_metadata_status
			end
			if not has_failed then
				if native.entry_type (entry) /= native.Symbolic_link_type then
					report_failure ("Source must be a symbolic link")
				else
					native.clear_entry_size (entry)
					set_entry_name (entry_name)
					native.write_header (handle, entry)
					check_status (native.last_status)
				end
			end
		ensure
			entry_owned: has_entry
			no_content: current_size = 0
			ready_to_finish: not has_failed implies is_entry_complete
		rescue
			abort
		end

	write_block
			-- Transfer one source block; mark EOF separately from successful writes.
		require
			active_file: is_open and has_entry
			not_finished: not is_entry_complete and not has_failed
		local
			status: INTEGER
		do
			last_written_bytes := 0
			native.pack_block (handle, source, byte_count.item)
			status := native.last_status
			if status = native.Archive_eof then
				if entry_written_bytes /= current_size.to_natural_64 then
					record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0, "Source size changed"))
				else
					is_entry_complete := True
				end
			else
				check_status (status)
				if not has_failed then
					last_written_bytes := byte_count.read_natural_64 (0)
					entry_written_bytes := entry_written_bytes + last_written_bytes
				end
			end
		ensure
			entry_retained: has_entry
			failed_block_not_counted: has_failed implies last_written_bytes = 0
			eof_not_counted: is_entry_complete implies last_written_bytes = 0

		rescue
			abort
		end

	finish_entry
			-- Finish a complete file or directory, close its source and free metadata.
		require
			complete_entry: is_open and has_entry and is_entry_complete and not has_failed
		do
			native.finish_entry (handle)
			check_status (native.last_status)
			release_source
			release_entry
		ensure
			entry_released: not has_entry
			writer_retained: is_open

		rescue
			abort
		end

feature -- Closing

	close
			-- Finalize a complete archive; discard owned output after any operation or cleanup failure.
			-- Closing a pending entry is unsuccessful; repeated calls are harmless.
		do
			if has_entry and not has_failed then
				report_failure ("Entry was not finished")
			end
			release_resources
		ensure
			closed: not is_open and not has_entry
			failure_preserved: old has_failed implies has_failed
		end

	abort
			-- Discard an unfinished owned output without translating an unexpected Eiffel exception.
			-- Already committed output is unaffected; repeated calls are harmless.
		do
			if owns_output then
				is_aborted := True
			end
			release_resources
		ensure
			closed: not is_open and not has_entry
		end

feature {ARC_ARCHIVE} -- Traversal failures

	is_output_source: BOOLEAN
			-- Did the latest checked ordinary source refer to this writer's output?

	check_output_source (path: PATH)
			-- Compare a source with the owned output while preserving primary and cleanup diagnostics.
		require
			ready: is_open and not has_failed and not has_entry
			valid_path: {ARC_PATHS}.is_valid_path (path)
		local
			name: NATIVE_STRING
		do
			name := path.native_string
			native.check_output_source (output_descriptor, name.item)
			is_output_source := native.last_is_output_source
			check_system_error (native.last_system_code, "Cannot compare source with output")
			check_system_error (native.last_cleanup_code, "Cannot close source identity handle")
		end

	report_failure (message: READABLE_STRING_8)
			-- Record an expected Eiffel-side traversal failure before cleanup starts.
		do
			record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0, message))
		end

	report_system_failure (code: INTEGER; message: READABLE_STRING_8)
			-- Record an expected ELKS I/O failure using its available system code.
		do
			record_failure (create {ARC_ERROR}.make_system (code, message))
		end

feature -- State

	is_aborted: BOOLEAN
			-- Was uncommitted output explicitly abandoned by `abort'?

	output_path: PATH
			-- Captured absolute output pathname, also used to exclude it from source traversal.

	is_open: BOOLEAN
			-- Does this object own an open archive writer?

	has_entry: BOOLEAN
			-- Is an owned file or directory entry awaiting finalization?

	is_entry_complete: BOOLEAN
			-- Has the current entry content completed successfully, before finalization?

	current_size: INTEGER_64
			-- Declared uncompressed size of the current entry; zero for a directory.
		require
			current_entry: has_entry
		do
			Result := size_snapshot
		end

	last_written_bytes: NATURAL_64
			-- Bytes successfully written by the latest block command; zero at EOF or error.

	has_failed: BOOLEAN
			-- Has an operation or cleanup failure been recorded?
		do
			Result := failure /= Void
		end

	last_error: ARC_ERROR
			-- Immutable primary failure, including subsequent cleanup failures.
		require
			failed: has_failed
		do
			check attached failure as value then
				Result := value
			end
		end

feature {NONE} -- Implementation

	open_archive (format: READABLE_STRING_8; password: detachable READABLE_STRING_32)
			-- Acquire output resources only after every attached Eiffel field is initialized.
		local
			name: NATIVE_STRING
			bytes: C_STRING
			converter: UTF_CONVERTER
		do
			handle := native.new_writer
			configure_format (format)
			if not has_failed and attached password as secret then
				if not format.same_string ({ARC_FORMATS}.Zip) then
					record_failure (create {ARC_ERROR}.make (native.Archive_failed, 0, "Encrypted output is supported only for ZIP with AES-256"))
				else
					create bytes.make (converter.utf_32_string_to_utf_8_string_8 (secret))
					native.set_write_password (handle, bytes.item)
					check_status (native.last_status)
					if not has_failed then
						native.enable_zip_encryption (handle)
						check_status (native.last_status)
					end
				end
			end
			if not has_failed then
				name := output_path.native_string
				native.create_output (name.item)
				output_descriptor := native.last_descriptor
				owns_output := output_descriptor >= 0
				check_system_error (native.last_system_code, "Cannot create output")
			end
			if not has_failed then
				native.open_writer (handle, output_descriptor)
				check_status (native.last_status)
				is_open := not has_failed
			end
			if has_failed then
				close
			end
		rescue
			abort
		end

	configure_format (format: READABLE_STRING_8)
			-- Configure a supported container and filter before creating the output.
		local
			filter: STRING_8
			bytes: C_STRING
		do
			if format.same_string ({ARC_FORMATS}.Zip) then
				native.set_zip (handle)
			elseif format.same_string ({ARC_FORMATS}.Seven_zip) then
				native.set_seven_zip (handle)
			else
				native.set_tar (handle)
			end
			check_status (native.last_status)

			if not has_failed and not format.same_string ({ARC_FORMATS}.Tar) and
				not format.same_string ({ARC_FORMATS}.Zip) and not format.same_string ({ARC_FORMATS}.Seven_zip)
			then
				if format.same_string ({ARC_FORMATS}.Tar_gz) then
					filter := "gzip"
				elseif format.same_string ({ARC_FORMATS}.Tar_bz2) then
					filter := "bzip2"
				elseif format.same_string ({ARC_FORMATS}.Tar_xz) then
					filter := "xz"
				elseif format.same_string ({ARC_FORMATS}.Tar_zstd) then
					filter := "zstd"
				else
					filter := "lz4"
				end
				create bytes.make (filter)
				native.add_filter (handle, bytes.item)
				check_status (native.last_status)
			end
		end

	check_system_error (code: INTEGER; stage: READABLE_STRING_8)
			-- Snapshot a standalone system failure before another native operation changes its diagnostic.
		do
			if code /= 0 then
				record_failure (create {ARC_ERROR}.make_system (code, stage))
			end
		end

	prepare_metadata_reader
			-- Acquire one reusable physical metadata reader and retain configuration errors.
		do
			if metadata_reader = default_pointer then
				metadata_reader := native.new_metadata_reader
				native.configure_metadata (metadata_reader)
				check_metadata_status
			end
		end

	check_metadata_status
			-- Snapshot metadata-reader errors independently of the archive writer.
		do
			if native.last_status /= native.Archive_ok then
				record_failure (create {ARC_ERROR}.make_native (metadata_reader, native.last_status))
			end
		end

	release_resources
			-- Finalize or abandon handles, then close the owned descriptor and remove failed output.
		local
			status: INTEGER
			name: NATIVE_STRING
		do
			if handle /= default_pointer then
				release_source
				release_entry
				if metadata_reader /= default_pointer then
					native.free_reader (metadata_reader)
					status := native.last_status
					metadata_reader := default_pointer
					if status /= native.Archive_ok then
						report_failure ("Cannot free source metadata reader")
					end
				end

				if has_failed or is_aborted then
					native.fail_writer (handle)
				else
					native.close_writer (handle)
					check_status (native.last_status)
				end
				native.free_writer (handle)
				status := native.last_status
				handle := default_pointer
				if status /= native.Archive_ok then
					record_failure (create {ARC_ERROR}.make (status, 0, "Cannot free archive writer"))
				end
			end

			if output_descriptor >= 0 then
				native.close_output (output_descriptor)
				output_descriptor := -1
				check_system_error (native.last_system_code, "Cannot close output")
			end
			if owns_output then
				if has_failed or is_aborted then
					name := output_path.native_string
					native.remove_output (name.item)
					check_system_error (native.last_system_code, "Cannot remove incomplete output")
				end
				owns_output := False
			end
			is_open := False
		end

	set_entry_name (name: READABLE_STRING_32)
			-- Copy a Unicode archive pathname through the explicit UTF-8 API.
		local
			converter: UTF_CONVERTER
			bytes: C_STRING
		do
			create bytes.make (converter.utf_32_string_to_utf_8_string_8 (name))
			native.entry_set_name (entry, bytes.item)
		end

	check_status (status: INTEGER)
			-- Snapshot a non-success native result before another call changes diagnostics.
		do
			if status /= native.Archive_ok then
				record_failure (create {ARC_ERROR}.make_native (handle, status))
			end
		end

	record_failure (value: ARC_ERROR)
			-- Keep the first immutable failure and append subsequent cleanup errors.
		do
			if attached failure as first then
				failure := first.with_cleanup_error (value)
			else
				failure := value
			end
		end

	release_source
			-- Close and consume an owned source, snapshotting any closing failure.
		local
			status: INTEGER
		do
			if source /= default_pointer then
				native.close_source (handle, source)
				status := native.last_status
				source := default_pointer
				check_status (status)
			end
		end

	release_entry
			-- Release owned entry metadata and invalidate its state queries.
		do
			if entry /= default_pointer then
				native.entry_free (entry)
				entry := default_pointer
			end
			has_entry := False
			is_entry_complete := False
			size_snapshot := 0
			entry_written_bytes := 0
		end

	native: ARC_NATIVE
			-- Native command adapters and immediately consumed result slots.

	cleanup_code: MANAGED_POINTER
			-- Separate int32_t source-open cleanup errno output slot.

	byte_count: MANAGED_POINTER
			-- Owned uint64_t output slot reused between transfers.

	handle: POINTER
			-- Owned archive writer; null outside an open operation.

	output_descriptor: INTEGER
			-- Owned descriptor borrowed by libarchive; -1 after closing.

	owns_output: BOOLEAN
			-- Did this operation create an uncommitted output that may need removal?

	metadata_reader: POINTER
			-- Owned metadata-only reader, never used for recursive traversal.

	source: POINTER
			-- Owned current FILE source, absent for directories.

	entry: POINTER
			-- Owned current metadata entry, released after finalization.

	size_snapshot: INTEGER_64
			-- Declared source size, valid only while an entry is owned.

	entry_written_bytes: NATURAL_64
			-- Successful bytes of the current source for EOF size validation.

	failure: detachable ARC_ERROR
			-- Latest immutable failure snapshot, absent on success.

invariant
	writer_owned_when_open: is_open = (handle /= default_pointer)
	open_owns_descriptor: is_open implies output_descriptor >= 0
	open_owns_output: is_open implies owns_output
	entry_ownership: has_entry = (entry /= default_pointer)
	entry_requires_writer: has_entry implies is_open
	source_requires_entry: source /= default_pointer implies has_entry
	complete_requires_entry: is_entry_complete implies has_entry
	pending_file_has_source: has_entry and not is_entry_complete and not has_failed implies source /= default_pointer
	nonnegative_size: size_snapshot >= 0

end
