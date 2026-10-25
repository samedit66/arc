note
	description: "Shared ownership of a native archive reader and immutable operation diagnostics."
	author: "samedit66"
	email: "samedit66@yandex.ru"

deferred class ARCHIVE_READ_SESSION

inherit
	ARCHIVE_CONSTANTS

feature {NONE} -- Initialization

	initialize_reader (a_path: PATH; a_password: detachable IMMUTABLE_STRING_32)
			-- Capture the source and immutable password without opening resources.
		do
			path := a_path.absolute_path
			password := a_password
		end

feature {NONE} -- Reader lifecycle

	open_reader
			-- Allocate the operation context and open its reader, retaining partial failures.
		require
			no_context: context = default_pointer
		local
			native_path: NATIVE_STRING
			password_bytes: detachable C_STRING
			password_pointer: POINTER
			encoding: ARCHIVE_TEXT_ENCODING
		do
			native_path := path.native_string
			context := c_new

			if attached password as value then
				create encoding
				create password_bytes.make (encoding.utf_8_bytes (value))
				password_pointer := password_bytes.item
			end

			check_status (c_open (context, native_path.item, password_pointer), Open_error)
		end

	advance_header
			-- Skip the current body before reading a header, keeping each failure's entry attribution.
		require
			has_context: context /= default_pointer
		do
			if has_header then
				header_status := c_skip (context)
				check_status (header_status, Read_error)
			end

			if error_snapshot = Void then
				diagnostic_entry_name := Void
				content_prepared := False
				header_status := c_next (context)
				check_status (header_status, Read_error)
				has_header := header_status = 0
			end
		end

	release_resources
			-- Release output resources before their reader and locale; preserve each cleanup error.
		do
			if context /= default_pointer then
				release_output
				check_status (c_close (context), Cleanup_error_code)
				check_status (c_free_handle (context), Cleanup_error_code)

				c_free (context)
				context := default_pointer
				has_header := False
			end
		ensure
			no_context: context = default_pointer
		end

	release_output
			-- Release specialized resources borrowing the reader or its locale, if any.
		do
		end

feature {NONE} -- Content validation

	prepare_content
			-- Capture a regular entry's declared size and sparse payload size before consuming it.
		do
			if not content_prepared then
				content_prepared := True
				content_bytes := 0
				content_extent := 0
				checks_content_size := c_is_regular (context) and then c_has_size (context)

				if checks_content_size then
					declared_size := c_size (context)
					expected_content_bytes := declared_size
					if declared_size < 0 then
						record_content_error ("Negative declared file size")
					else
						capture_sparse_size
					end
				end
			end
		end

	capture_sparse_size
			-- Sum declared sparse data lengths, keeping every extent and addition within the logical size.
		require
			nonnegative_size: declared_size >= 0
		local
			count, index: INTEGER
			offset, length: INTEGER_64
		do
			count := c_sparse_reset (context)
			if count > 0 then
				expected_content_bytes := 0
			end

			from
			invariant
				bounded_payload: expected_content_bytes >= 0 and expected_content_bytes <= declared_size
			until
				index = count or error_snapshot /= Void
			loop
				check_status (c_sparse_next (context), Read_error)
				if error_snapshot = Void then
					offset := c_sparse_offset (context)
					length := c_sparse_length (context)
					if offset < 0 or length < 0 or else offset > declared_size or else
						length > declared_size - offset or else length > declared_size - expected_content_bytes
					then
						record_content_error ("Invalid sparse extent or payload size")
					else
						expected_content_bytes := expected_content_bytes + length
					end
				end
				index := index + 1
			end
		end

	validate_content_block (status: INTEGER)
			-- Check successful block extents and exact payload length at EOF, including sparse holes.
		local
			offset, size: INTEGER_64
			native_size: NATURAL_64
		do
			if error_snapshot = Void and checks_content_size then
				if status = End_status then
					if content_bytes /= expected_content_bytes then
						record_content_error ("Decoded file size differs from declared payload size")
					end
				elseif status = 0 then
					offset := c_block_offset (context)
					native_size := c_block_size (context)
					if offset < content_extent or else offset > declared_size or else
						native_size > (declared_size - offset).to_natural_64 or else
						native_size > (expected_content_bytes - content_bytes).to_natural_64
					then
						record_content_error ("Decoded block exceeds declared file size or has an invalid offset")
					else
						size := native_size.to_integer_64
						content_extent := offset + size
						content_bytes := content_bytes + size
					end
				end
			end
		end

	record_content_error (message: READABLE_STRING_8)
			-- Record a malformed ordinary-file size as an expected reading failure.
		do
			record_failure (create {ARCHIVE_ERROR}.make (Read_error, "validate entry size", message,
				path, diagnostic_entry_name, 0, "none", 0))
		end

feature {NONE} -- Diagnostics

	check_status (status, code: INTEGER)
			-- Snapshot a native failure before another native operation can replace it.
		do
			if status < 0 then
				record_failure (create {ARCHIVE_ERROR}.make_diagnostic (c_error (context), code, path, diagnostic_entry_name))
			end
		end

	record_failure (failure: ARCHIVE_ERROR)
			-- Preserve the first failure and append subsequent cleanup failures.
		do
			if attached error_snapshot as primary then
				error_snapshot := primary.with_cleanup_error (failure)
			else
				error_snapshot := failure
			end
		end

feature {NONE} -- Storage

	path: PATH
			-- Absolute archive source path.

	password: detachable IMMUTABLE_STRING_32
			-- Captured password, absent by default.

	context: POINTER
			-- Owned native context whose first member is an arc_read_context.

	error_snapshot: detachable ARCHIVE_ERROR
			-- Primary operation failure and subsequent cleanup errors.

	diagnostic_entry_name: detachable STRING_32
			-- Original current entry name, absent while reading an unidentified header.

	header_status: INTEGER
			-- Result of the latest native header advance.

	has_header: BOOLEAN
			-- Has a current native header been read whose remaining data may need skipping?

feature {NONE} -- Content state

	content_prepared: BOOLEAN
			-- Have content-validation inputs been captured for this entry?

	checks_content_size: BOOLEAN
			-- Does this ordinary non-hardlinked entry have a declared size?

	declared_size: INTEGER_64
			-- Declared logical size, including sparse holes.

	expected_content_bytes: INTEGER_64
			-- Declared payload size, excluding sparse holes.

	content_bytes: INTEGER_64
			-- Successfully decoded payload bytes of the current entry.

	content_extent: INTEGER_64
			-- End of the last successfully decoded data block.

feature {NONE} -- Native primitives

	c_new: POINTER
			-- Fresh native context with an embedded reader at its start.
		external
			"C use <arc_read.h>, %"eif_file.h%""
		alias
			"arc_read_new"
		end

	c_open (a_context, a_path, a_password: POINTER): INTEGER
			-- Open the native reader using the captured inputs.
		external
			"C use <arc_read.h>"
		alias
			"arc_read_open"
		end

	c_next (a_context: POINTER): INTEGER
			-- Advance to the next native header.
		external
			"C use <arc_read.h>"
		alias
			"arc_read_next"
		end

	c_skip (a_context: POINTER): INTEGER
			-- Skip the current entry's remaining data before attempting another header.
		external
			"C use <arc_read.h>"
		alias
			"arc_read_skip"
		end

	c_close (a_context: POINTER): INTEGER
			-- Close the native input handle.
		external
			"C use <arc_read.h>"
		alias
			"arc_read_close"
		end

	c_free_handle (a_context: POINTER): INTEGER
			-- Free the native input handle, retaining context diagnostics.
		external
			"C use <arc_read.h>"
		alias
			"arc_read_free_handle"
		end

	c_free (a_context: POINTER)
			-- Dispose of context storage after all handles are released.
		external
			"C use <arc_read.h>"
		alias
			"arc_read_free"
		end

	c_error (a_context: POINTER): POINTER
			-- Borrow the current native diagnostic until the next native call.
		external
			"C inline use <arc_read.h>"
		alias
			"return &((struct arc_read_context *)$a_context)->error;"
		end


	c_sparse_next (a_context: POINTER): INTEGER
			-- Advance the borrowed native sparse map, preserving failures.
		external
			"C use <arc_read.h>"
		alias
			"arc_read_sparse_next"
		end

	c_is_regular (a_context: POINTER): BOOLEAN
			-- Is this an ordinary-file entry carrying its own contents?
		external
			"C inline use <arc_read.h>"
		alias
			"return archive_entry_filetype(((struct arc_read_context *)$a_context)->entry) == AE_IFREG && !archive_entry_hardlink_is_set(((struct arc_read_context *)$a_context)->entry);"
		end

	c_has_size (a_context: POINTER): BOOLEAN
			-- Does the native entry declare a logical size?
		external
			"C inline use <arc_read.h>"
		alias
			"return archive_entry_size_is_set(((struct arc_read_context *)$a_context)->entry);"
		end

	c_size (a_context: POINTER): INTEGER_64
			-- Declared native logical size.
		external
			"C inline use <arc_read.h>"
		alias
			"return archive_entry_size(((struct arc_read_context *)$a_context)->entry);"
		end

	c_sparse_reset (a_context: POINTER): INTEGER
			-- Reset the native sparse cursor and return its extent count.
		external
			"C inline use <arc_read.h>"
		alias
			"return archive_entry_sparse_reset(((struct arc_read_context *)$a_context)->entry);"
		end

	c_sparse_offset (a_context: POINTER): INTEGER_64
			-- Current sparse extent offset.
		external
			"C inline use <arc_read.h>"
		alias
			"return ((struct arc_read_context *)$a_context)->sparse_offset;"
		end

	c_sparse_length (a_context: POINTER): INTEGER_64
			-- Current sparse extent length.
		external
			"C inline use <arc_read.h>"
		alias
			"return ((struct arc_read_context *)$a_context)->sparse_length;"
		end

	c_block_size (a_context: POINTER): NATURAL_64
			-- Last successfully read block size.
		external
			"C inline use <arc_read.h>"
		alias
			"return ((struct arc_read_context *)$a_context)->block_size;"
		end

	c_block_offset (a_context: POINTER): INTEGER_64
			-- Last successfully read block offset.
		external
			"C inline use <arc_read.h>"
		alias
			"return ((struct arc_read_context *)$a_context)->block_offset;"
		end

end
