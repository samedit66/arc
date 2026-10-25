note
	description: "Immutable archive failure with separate native diagnostics and cleanup errors."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_ERROR

inherit
	ARCHIVE_CONSTANTS

create {ARCHIVE_READ_SESSION, ARCHIVE_UNPACK_SESSION, ARCHIVE_PACK_SESSION, ARCHIVE_READ_STREAM, ARCHIVE_ERROR}
	make,
	make_diagnostic

feature {NONE} -- Initialization

	make (a_code: INTEGER; a_stage, a_message: READABLE_STRING_8; a_source: PATH;
		an_entry: detachable READABLE_STRING_32; a_native_status: INTEGER;
		a_system_domain: READABLE_STRING_8; a_system_code: INTEGER)
			-- Create an immutable failure snapshot from diagnostic bytes.
		local
			encoding: ARCHIVE_TEXT_ENCODING
		do
			code := a_code
			create stage.make_from_string (a_stage)
			create raw_message.make_from_string (a_message)
			create encoding
			create message.make_from_string (encoding.text_from_utf_8_bytes (a_message))

			source_path := a_source
			if attached an_entry as name then
				create entry_name.make_from_string (name)
			end

			native_status := a_native_status
			create system_domain.make_from_string (a_system_domain)
			system_code := a_system_code

			create cleanup_items.make (0)
		end

	make_diagnostic (a_context: POINTER; a_code: INTEGER; a_source: PATH; an_entry: detachable READABLE_STRING_32)
			-- Copy the current native diagnostic before another native call can replace it.
		local
			stage_bytes, message_bytes, domain_bytes: STRING_8
		do
			create stage_bytes.make_from_c (c_stage (a_context))
			create message_bytes.make_from_c (c_message (a_context))
			create domain_bytes.make_from_c (c_domain (a_context))

			make (a_code, stage_bytes, message_bytes, a_source, an_entry,
				c_status (a_context), domain_bytes, c_system_code (a_context))
		end

feature -- Access

	code: INTEGER
			-- Category identifier; compare symbolically, never persist the numeric value.

	stage: IMMUTABLE_STRING_8
			-- Operation stage that failed.

	message: IMMUTABLE_STRING_32
			-- Bytes interpreted as UTF-8 with lossless escapes; native encoding may differ.

	raw_message: IMMUTABLE_STRING_8
			-- Original diagnostic bytes, preserved without decoding or truncation.

	source_path: PATH
			-- Archive path associated with the failure (input for reading, output for writing).

	entry_name: detachable IMMUTABLE_STRING_32
			-- Original archive entry name, if failure occurred while processing an entry.

	native_status: INTEGER
			-- Libarchive status; zero for a non-libarchive failure.

	system_domain: IMMUTABLE_STRING_8
			-- Error-code namespace: "errno", "win32", or "none".

	system_code: INTEGER
			-- Error code in `system_domain'; zero when unavailable.

	cleanup_error_count: INTEGER
			-- Number of cleanup failures recorded after the primary failure.
		do
			Result := cleanup_items.count
		end

	cleanup_error (an_index: INTEGER): ARCHIVE_ERROR
			-- Immutable cleanup failure at `an_index'.
		require
			valid_index: an_index >= 1 and an_index <= cleanup_error_count
		do
			Result := cleanup_items [an_index]
		end

feature {ARCHIVE_READ_SESSION, ARCHIVE_UNPACK_SESSION, ARCHIVE_PACK_SESSION, ARCHIVE_READ_STREAM} -- Snapshot construction

	with_cleanup_error (an_error: ARCHIVE_ERROR): ARCHIVE_ERROR
			-- New snapshot preserving this failure and appending `an_error'.
		do
			create Result.make (code, stage, raw_message, source_path, entry_name,
				native_status, system_domain, system_code)

			across cleanup_items as item loop
				Result.cleanup_items.extend (item)
			end

			Result.cleanup_items.extend (an_error)
		end

feature {ARCHIVE_ERROR} -- Storage

	cleanup_items: ARRAYED_LIST [ARCHIVE_ERROR]
			-- Cleanup failures owned exclusively by this immutable snapshot.

feature {NONE} -- Native diagnostic access

	c_stage (a_context: POINTER): POINTER
		external
			"C inline use <arc_native.h>"
		alias
			"return (void *)((struct arc_native_error *)$a_context)->stage;"
		end

	c_message (a_context: POINTER): POINTER
		external
			"C inline use <arc_native.h>"
		alias
			"return ((struct arc_native_error *)$a_context)->message;"
		end

	c_domain (a_context: POINTER): POINTER
		external
			"C inline use <arc_native.h>"
		alias
			"return (void *)((struct arc_native_error *)$a_context)->system_domain;"
		end

	c_status (a_context: POINTER): INTEGER
		external
			"C inline use <arc_native.h>"
		alias
			"return ((struct arc_native_error *)$a_context)->native_status;"
		end

	c_system_code (a_context: POINTER): INTEGER
		external
			"C inline use <arc_native.h>"
		alias
			"return ((struct arc_native_error *)$a_context)->system_code;"
		end

end
