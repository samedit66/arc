note
	description: "Owner of one extraction operation; policy and orchestration stay in Eiffel."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_UNPACK_SESSION

inherit
	ARCHIVE_READ_SESSION
		rename
			diagnostic_entry_name as entry_name
		redefine
			c_new,
			c_free,
			release_output
		end

create {ARCHIVE_READER}
	make

feature {NONE} -- Initialization

	make (a_source, a_destination: PATH; options: UNPACK_OPTIONS)
			-- Prepare an extraction session without opening files.
		do
			initialize_reader (a_source, options.password)
			destination := a_destination
			progress_callback := options.progress_callback
		end

feature {ARCHIVE_READER} -- Operations

	unpack
			-- Extract the source, record its result, and release native resources.
		local
			destination_name, resolved_name: NATIVE_STRING
		do
			notify_progress (False)
			open_reader

			if last_error = Void then
				check_status (c_open_writer (context), Open_error)
			end

			destination_name := destination.native_string

			if last_error = Void then
				check_status (c_create_destination (context, destination_name.item), Destination_error)
			end

			if last_error = Void then
				create resolved_name.make_from_pointer (c_destination (context))
				create destination.make_from_string (resolved_name.string)
				extract_entries
			end

			release_resources

		rescue
			-- Unexpected Eiffel exceptions propagate after cleanup, not as operation errors.
			release_resources
		end

	notify_finished
			-- Notify final success after ARCHIVE_READER has published the result and resources are gone.
		do
			entry_name := Void
			entry_written_bytes := 0

			notify_progress (True)
		end

feature {ARCHIVE_READER} -- Result

	last_error: detachable ARCHIVE_ERROR
			-- Primary failure and cleanup diagnostics, absent after successful extraction.
		do
			Result := error_snapshot
		end

feature {NONE} -- Extraction

	extract_entries
		do
			from
				read_next_entry
			until
				last_error /= Void or archive_finished
			loop
				prepare_entry_paths

				if last_error = Void then
					prepare_content
				end

				if last_error = Void then
					check_status (c_begin_entry (context), Write_error)
				end

				if last_error = Void then
					transfer_entry
				end

				if last_error = Void then
					check_status (c_finish_entry (context), Write_error)
				end

				if last_error = Void then
					completed_entries := completed_entries + 1
					notify_progress (False)
					read_next_entry
				end
			end
		end

	read_next_entry
		local
			status: INTEGER
		do
			entry_written_bytes := 0
			advance_header
			status := header_status
			archive_finished := status = End_status
		end

	prepare_entry_paths
		local
			name_pointer, target_pointer: POINTER
			native_name: NATIVE_STRING
			name, target: STRING_32
			output_name: NATIVE_STRING
			target_name: detachable NATIVE_STRING
			policy: ARCHIVE_ENTRY_PATHS
		do
			create policy
			name_pointer := c_entry_name (context, False)
			target_pointer := c_entry_name (context, True)

			if name_pointer = default_pointer then
				record_path_error ("entry pathname", "Cannot convert entry pathname")
			else
				create native_name.make_from_pointer (name_pointer)
				name := native_name.string
				entry_name := name

				if not policy.is_valid_name (name) then
					record_path_error ("entry pathname", "Invalid or unrepresentable relative pathname")
				elseif not c_supported_entry (context) then
					record_policy_failure (Unsupported_entry_error, "entry type",
						"Only files, directories and links are supported", path, entry_name)
				elseif c_has_hardlink (context) and target_pointer = default_pointer then
					record_path_error ("hardlink target", "Cannot convert hardlink pathname")
				else
					output_name := destination.extended (name).native_string

					if target_pointer /= default_pointer then
						create native_name.make_from_pointer (target_pointer)
						target := native_name.string

						if policy.is_valid_name (target) then
							target_name := destination.extended (target).native_string
						else
							record_path_error ("hardlink target", "Invalid relative hardlink target")
						end
					end

					if last_error = Void then
						if attached target_name as link then
							c_set_paths (context, output_name.item, link.item)
						else
							c_set_paths (context, output_name.item, default_pointer)
						end
					end
				end
			end
		end

	transfer_entry
		local
			status: INTEGER
		do
			from
				status := c_transfer_block (context)
				check_transfer_status (status)
			until
				last_error /= Void or status = End_status
			loop
				entry_written_bytes := entry_written_bytes + c_last_block_size (context)
				written_bytes := written_bytes + c_last_block_size (context)

				notify_progress (False)

				status := c_transfer_block (context)
				check_transfer_status (status)
			end
		end

feature {NONE} -- Progress notifications

	notify_progress (finished: BOOLEAN)
		local
			progress: UNPACK_PROGRESS
		do
			if attached progress_callback as callback then
				create progress.make (entry_name, entry_written_bytes, written_bytes, completed_entries, finished)

				callback.call ([progress])
			end
		end

feature {NONE} -- Failure recording and resource ownership

	check_transfer_status (a_status: INTEGER)
		do
			if a_status = Write_failure_status then
				check_status (a_status, Write_error)
			else
				check_status (a_status, Read_error)
			end
			validate_content_block (a_status)
		end

	record_path_error (a_stage: READABLE_STRING_8; a_message: READABLE_STRING_GENERAL)
		do
			record_policy_failure (Path_error, a_stage, a_message, path, entry_name)
		end

	release_output
			-- Release the disk writer before the common reader and locale.
		do
			check_status (c_close_writer (context), Cleanup_error_code)
			check_status (c_free_writer (context), Cleanup_error_code)
		end

feature {NONE} -- Error snapshots

	record_policy_failure (a_code: INTEGER; a_stage: READABLE_STRING_8; a_message: READABLE_STRING_GENERAL;
		a_source: PATH; an_entry: detachable READABLE_STRING_32)
		local
			failure: ARCHIVE_ERROR
			encoding: ARCHIVE_TEXT_ENCODING
		do
			create encoding
			create failure.make (a_code, a_stage, encoding.utf_8_bytes (a_message),
				a_source, an_entry, 0, "none", 0)

			record_failure (failure)
		end

feature {NONE} -- State

	destination: PATH

	archive_finished: BOOLEAN

	written_bytes: NATURAL_64

	entry_written_bytes: NATURAL_64

	progress_callback: detachable PROCEDURE [TUPLE [progress: UNPACK_PROGRESS]]

	completed_entries: NATURAL_64

feature {NONE} -- Native primitives

	c_new: POINTER
			-- Fresh extraction context with an embedded reader.
		external
			"C use <arc_unpack.h>, %"eif_file.h%""
		alias
			"arc_unpack_new"
		end

	c_open_writer (a_context: POINTER): INTEGER
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_open_writer"
		end

	c_create_destination (a_context, a_path: POINTER): INTEGER
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_create_destination"
		end

	c_entry_name (a_context: POINTER; hardlink: BOOLEAN): POINTER
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_entry_name"
		end

	c_set_paths (a_context, a_path, a_target: POINTER)
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_set_paths"
		end

	c_begin_entry (a_context: POINTER): INTEGER
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_begin_entry"
		end

	c_transfer_block (a_context: POINTER): INTEGER
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_transfer_block"
		end

	c_finish_entry (a_context: POINTER): INTEGER
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_finish_entry"
		end

	c_close_writer (a_context: POINTER): INTEGER
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_close_writer"
		end

	c_free_writer (a_context: POINTER): INTEGER
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_free_writer"
		end

	c_free (a_context: POINTER)
			-- Dispose of the extraction context after releasing its writer and reader.
		external
			"C use <arc_unpack.h>"
		alias
			"arc_unpack_free"
		end

	c_last_block_size (a_context: POINTER): NATURAL_64
		external
			"C inline use <arc_unpack.h>"
		alias
			"return ((struct arc_unpack_context *)$a_context)->last_block_size;"
		end

	c_destination (a_context: POINTER): POINTER
		external
			"C inline use <arc_unpack.h>"
		alias
			"return ((struct arc_unpack_context *)$a_context)->destination;"
		end

	c_has_hardlink (a_context: POINTER): BOOLEAN
		external
			"C inline use <arc_unpack.h>"
		alias
			"return archive_entry_hardlink_is_set(((struct arc_unpack_context *)$a_context)->input.entry);"
		end

	c_supported_entry (a_context: POINTER): BOOLEAN
		external
			"C inline use <arc_unpack.h>"
		alias
			"struct archive_entry *e = ((struct arc_unpack_context *)$a_context)->input.entry; unsigned int t = archive_entry_filetype(e); return t == AE_IFREG || t == AE_IFDIR || t == AE_IFLNK || archive_entry_hardlink_is_set(e);"
		end

end
