note
	description: "Owner of one packing operation; traversal, names and observers stay in Eiffel."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_PACK_SESSION

inherit
	ARCHIVE_FORMATS

create {ARCHIVE_WRITER}
	make

feature {NONE} -- Initialization

	make (a_path: PATH; a_sources: ARRAYED_LIST [ARCHIVE_PACK_SOURCE]; options: PACK_OPTIONS)
			-- Capture the inputs before the first notification, without opening files.
		do
			path := a_path
			sources := a_sources
			password := options.password
			progress_callback := options.progress_callback
			format := options.format
			compression := options.compression
			if options.is_filename_encoding then
				infer_encoding
			end
			create names.make (32)
			create parents.make (32)
		end

feature {ARCHIVE_WRITER} -- Operations

	pack
			-- Create the archive and release resources, removing incomplete output on failure.
		local
			output: NATIVE_STRING
			format_bytes, compression_bytes: C_STRING
			password_bytes: detachable C_STRING
			password_pointer: POINTER
			encoding: ARCHIVE_TEXT_ENCODING
		do
			notify_progress (False)
			if format = Unknown_format then
				fail (Open_error, "configure encoding", "Unrecognized output suffix")
			else
				context := c_new
				create format_bytes.make (format_name)
				create compression_bytes.make (compression_name)
				if attached password as value then
					create encoding
					create password_bytes.make (encoding.utf_8_bytes (value))
					password_pointer := password_bytes.item
				end

				check_status (c_configure (context, format_bytes.item, compression_bytes.item, password_pointer), Open_error)
				if last_error = Void then
					output := path.native_string
					check_status (c_create_output (context, output.item), Open_error)
				end

				across sources as source loop
					if last_error = Void then
						pack_source (source)
					end
				end
			end

			release_resources (False)
		rescue
			release_resources (True)
		end

	notify_finished
			-- Notify final success after the writer has published its result.
		do
			entry_name := Void
			entry_written_bytes := 0
			notify_progress (True)
		end

feature {ARCHIVE_WRITER} -- Result

	last_error: detachable ARCHIVE_ERROR
			-- Primary failure with separate cleanup diagnostics, absent on success.

feature {NONE} -- Encoding

	infer_encoding
		local
			suffix: STRING_32
		do
			suffix := output_suffix (path.name)
			format := format_for_suffix (suffix)
			compression := compression_for_suffix (suffix)
		end

	format_name: STRING_8
		do
			if format = Zip then
				Result := "zip"
			elseif format = Tar then
				Result := "tar"
			else
				Result := "7z"
			end
		end

	compression_name: STRING_8
		do
			if compression = Gzip then
				Result := "gzip"
			elseif compression = Bzip2 then
				Result := "bzip2"
			elseif compression = Xz then
				Result := "xz"
			elseif compression = Zstd then
				Result := "zstd"
			else
				Result := "none"
			end
		end

feature {NONE} -- Traversal

	pack_source (source: ARCHIVE_PACK_SOURCE)
		local
			native: NATIVE_STRING
			root_name, disk_name, stored_name: STRING_32
			path_bytes: STRING_8
			encoding: ARCHIVE_TEXT_ENCODING
			status: INTEGER
			first: BOOLEAN
		do
			entry_name := source.entry_name.to_string_32.twin
			entry_written_bytes := 0
			native := source.path.native_string
			check_status (c_open_source (context, native.item), Read_error)
			create root_name.make_empty
			first := True
			from
			until
				last_error /= Void or status = End_status
			loop
				status := c_next (context)
				check_status (status, Read_error)
				if last_error = Void and status /= End_status then
					check_status (c_check_output (context), Read_error)
					check_status (c_close_identity (context), Cleanup_error_code)
				end
				if last_error = Void and status /= End_status then
					create path_bytes.make_from_c (c_entry_path (context))
					create encoding
					disk_name := encoding.text_from_utf_8_bytes (path_bytes)
					if first then
						root_name := disk_name
						stored_name := source.entry_name.to_string_32.twin
					else
						stored_name := descendant_name (disk_name, root_name, source.entry_name)
					end

					entry_name := stored_name
					entry_written_bytes := 0
					if c_is_output (context) then
						-- Exclude the output encountered inside the selected tree.
					elseif stored_name.is_empty then
						if c_is_directory (context) then
							check_status (c_descend (context), Read_error)
						else
							fail (Path_error, "source root", "An empty archive name requires a directory")
						end
					else
						pack_entry (stored_name)
					end
					first := False
				end
			end

			close_source
		end

	descendant_name (disk_name, root_name: STRING_32; archive_root: IMMUTABLE_STRING_32): STRING_32
			-- Fresh archive name for a descendant of the current source root.
		do
			Result := disk_name.substring (root_name.count + 1, disk_name.count)
			if {PLATFORM}.is_windows then
				Result.replace_substring_all ("\", "/")
			end

			from until Result.is_empty or else Result [1] /= '/' loop
				Result.remove_head (1)
			end

			if not archive_root.is_empty then
				Result.prepend (archive_root + "/")
			end
		end

	pack_entry (name: STRING_32)
		local
			bytes: C_STRING
			encoding: ARCHIVE_TEXT_ENCODING
			policy: ARCHIVE_PACK_NAMES
		do
			create policy
			if not policy.is_valid_name (name) then
				fail (Path_error, "entry name", "Invalid relative archive pathname")
			elseif not c_is_supported (context) then
				fail (Unsupported_entry_error, "source type", "Only files, directories and symbolic links are supported")
			else
				register_name (name, c_is_directory (context))
			end

			if last_error = Void then
				create encoding
				create bytes.make (encoding.utf_8_bytes (name))
				check_status (c_begin_entry (context, bytes.item), Write_error)
				if last_error = Void and c_is_regular (context) then
					transfer_entry_data
				end

				if last_error = Void then
					check_status (c_finish_entry (context), Write_error)
				end
				if last_error = Void then
					completed_entries := completed_entries + 1
					notify_progress (False)
					if c_is_directory (context) then
						check_status (c_descend (context), Read_error)
					end
				end
			end
		end

	transfer_entry_data
			-- Transfer regular-file blocks and notify only after successful writes.
		local
			status: INTEGER
		do
			from
			until
				last_error /= Void or status = End_status
			loop
				status := c_transfer (context)
				if status = Read_failure_status then
					check_status (status, Read_error)
				else
					check_status (status, Write_error)
				end

				if status = 0 then
					entry_written_bytes := entry_written_bytes + c_last_block_size (context)
					written_bytes := written_bytes + c_last_block_size (context)
					notify_progress (False)
				end
			end
		end

	register_name (name: STRING_32; a_directory: BOOLEAN)
		local
			parent: STRING_32
			separator: INTEGER
		do
			if names.has (name) then
				fail (Path_error, "entry name", "Duplicate archive entry name")
			else
				parent := name.twin
				from
					separator := parent.last_index_of ('/', parent.count)
				until
					separator = 0 or last_error /= Void
				loop
					parent.keep_head (separator - 1)
					parents.force (True, parent.twin)
					if names.has (parent) and then not names [parent] then
						fail (Path_error, "entry name", "Archive ancestor is not a directory")
					end
					separator := parent.last_index_of ('/', parent.count)
				end
				if not a_directory and then parents.has (name) then
					fail (Path_error, "entry name", "Archive entry conflicts with an existing subtree")
				end
				if last_error = Void then
					names.put (a_directory, name.twin)
				end
			end
		end

feature {NONE} -- Results and lifecycle

	notify_progress (finished: BOOLEAN)
		local
			progress: PACK_PROGRESS
		do
			if attached progress_callback as callback then
				create progress.make (entry_name, entry_written_bytes, written_bytes, completed_entries, finished)
				callback.call ([progress])
			end
		end

	fail (code: INTEGER; stage, message: STRING_8)
		do
			record_failure (create {ARCHIVE_ERROR}.make (code, stage, message, path, entry_name, 0, "none", 0))
		end

	check_status (status, code: INTEGER)
		do
			if status < 0 then
				record_failure (create {ARCHIVE_ERROR}.make_diagnostic (c_error (context), code, path, entry_name))
			end
		end

	record_failure (failure: ARCHIVE_ERROR)
		do
			if attached last_error as primary then
				last_error := primary.with_cleanup_error (failure)
			else
				last_error := failure
			end
		end

	close_source
		do
			check_status (c_close_disk (context), Cleanup_error_code)
			check_status (c_free_disk (context), Cleanup_error_code)
		end

	release_resources (interrupted: BOOLEAN)
		do
			if context /= default_pointer then
				check_status (c_close_identity (context), Cleanup_error_code)
				close_source
				check_status (c_close_writer (context), Cleanup_error_code)
				check_status (c_free_writer (context), Cleanup_error_code)
				check_status (c_close_output (context), Cleanup_error_code)
				if interrupted or last_error /= Void then
					check_status (c_remove_output (context), Cleanup_error_code)
				end
				c_free (context)
				context := default_pointer
			end
		end

feature {NONE} -- State

	path: PATH

	sources: ARRAYED_LIST [ARCHIVE_PACK_SOURCE]
			-- Borrowed writer list; registration is forbidden throughout packing.

	password: detachable IMMUTABLE_STRING_32

	progress_callback: detachable PROCEDURE [TUPLE [progress: PACK_PROGRESS]]

	format, compression: INTEGER

	context: POINTER

	names: HASH_TABLE [BOOLEAN, STRING_32]

	parents: HASH_TABLE [BOOLEAN, STRING_32]

	entry_name: detachable STRING_32

	entry_written_bytes, written_bytes, completed_entries: NATURAL_64

feature {NONE} -- Native primitives

	c_close_identity (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_close_identity"
		end

	c_check_output (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_check_output"
		end

	c_new: POINTER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_new"
		end

	c_configure (a_context, a_format, a_compression, a_password: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_configure"
		end

	c_create_output (a_context, a_path: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_create_output"
		end

	c_open_source (a_context, a_path: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_open_source"
		end

	c_next (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_next"
		end

	c_descend (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_descend"
		end

	c_begin_entry (a_context, a_name: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_begin_entry"
		end

	c_transfer (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_transfer"
		end

	c_finish_entry (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_finish_entry"
		end

	c_close_disk (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_close_disk"
		end

	c_free_disk (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_free_disk"
		end

	c_close_writer (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_close_writer"
		end

	c_free_writer (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_free_writer"
		end

	c_close_output (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_close_output"
		end

	c_remove_output (a_context: POINTER): INTEGER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_remove_output"
		end

	c_free (a_context: POINTER)
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_free"
		end

	c_entry_path (a_context: POINTER): POINTER
		external
			"C use <arc_pack.h>"
		alias
			"arc_pack_entry_path"
		end

	c_error (a_context: POINTER): POINTER
		external
			"C inline use <arc_pack.h>"
		alias
			"return &((struct arc_pack_context *)$a_context)->error;"
		end

	c_is_directory (a_context: POINTER): BOOLEAN
		external
			"C inline use <arc_pack.h>"
		alias
			"return archive_entry_filetype(((struct arc_pack_context *)$a_context)->entry) == AE_IFDIR;"
		end

	c_is_regular (a_context: POINTER): BOOLEAN
		external
			"C inline use <arc_pack.h>"
		alias
			"return archive_entry_filetype(((struct arc_pack_context *)$a_context)->entry) == AE_IFREG;"
		end

	c_is_supported (a_context: POINTER): BOOLEAN
		external
			"C inline use <arc_pack.h>"
		alias
			"return archive_entry_filetype(((struct arc_pack_context *)$a_context)->entry) == AE_IFREG || archive_entry_filetype(((struct arc_pack_context *)$a_context)->entry) == AE_IFDIR || archive_entry_filetype(((struct arc_pack_context *)$a_context)->entry) == AE_IFLNK;"
		end

	c_is_output (a_context: POINTER): BOOLEAN
		external
			"C inline use <arc_pack.h>"
		alias
			"return ((struct arc_pack_context *)$a_context)->is_output;"
		end

	c_last_block_size (a_context: POINTER): NATURAL_64
		external
			"C inline use <arc_pack.h>"
		alias
			"return ((struct arc_pack_context *)$a_context)->last_block_size;"
		end

end
