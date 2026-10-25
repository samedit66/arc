note
	description: "One explicitly opened sequential archive traversal, without an extraction writer."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_READ_STREAM

inherit
	ARCHIVE_READ_SESSION
		export
			{ANY} path
		end

	ITERABLE [ARCHIVE_ENTRY]

create
	make,
	make_with_options

feature {NONE} -- Initialization

	make (a_path: PATH)
			-- Capture the absolute path without opening it; use no password.
		require
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.name.has ('%U')
		do
			make_with_options (a_path, create {ARCHIVE_READ_OPTIONS}.make)
		end

	make_with_options (a_path: PATH; options: ARCHIVE_READ_OPTIONS)
			-- Capture the absolute path and immutable password without retaining `options'.
		require
			path_not_empty: not a_path.is_empty
			path_without_nul: not a_path.name.has ('%U')
		do
			initialize_reader (a_path, options.password)
			encryption_status := Encryption_unknown
			create filter_names.make (0)
		end

feature -- Access

	item: ARCHIVE_ENTRY
			-- Immutable snapshot of the current entry, retainable after closing.
		require
			valid_position: not after
		do
			check attached current_entry as entry then
				Result := entry
			end
		end

	new_cursor: ARCHIVE_READ_CURSOR
			-- Fresh adapter to the shared position; no I/O or rewind.
			-- Use one traversal at a time; do not interleave adapters or manual advancement.
		do
			create Result.make (Current)
		end

	last_error: ARCHIVE_ERROR
			-- Immutable primary failure and subsequent cleanup failures.
		require
			failed: has_failed
		do
			check attached error_snapshot as failure then
				Result := failure
			end
		end

feature -- Archive metadata

	format_name: detachable IMMUTABLE_STRING_32
			-- Last detected format, retained after closing; absent before detection.

	filter_count: INTEGER
			-- Number of detected outer filters, excluding the native input pseudo-filter.
		do
			Result := filter_names.count
		end

	filter_name (an_index: INTEGER): IMMUTABLE_STRING_32
			-- Retained outer filter name, ordered from the format towards the input.
		require
			valid_index: an_index >= 1 and an_index <= filter_count
		do
			Result := filter_names [an_index]
		end

	encryption_status: INTEGER
			-- Last detected encryption state; unknown before reading and retained after closing.

feature -- Status report

	is_started: BOOLEAN
			-- Has this single-use stream attempted to open its archive?

	is_open: BOOLEAN
			-- Does the stream own an open reader?

	after: BOOLEAN
			-- Is there no current entry because the stream is unopened, ended, failed or closed?
		do
			Result := current_entry = Void
		end

	has_failed: BOOLEAN
			-- Has an expected reading or cleanup failure occurred?
		do
			Result := error_snapshot /= Void
		end

	is_complete: BOOLEAN
			-- Was normal archive EOF reached and all resources successfully released?
		do
			Result := reached_end and not is_open and not has_failed
		end

feature -- Commands

	open
			-- Open once and position at the first header; close automatically on EOF or failure.
		require
			not_started: not is_started
		do
			is_started := True
			open_reader

			if not has_failed then
				is_open := True
				read_next_header
			else
				close
			end
		ensure
			started: is_started
			terminal_closed: after implies not is_open
		rescue
			close
		end

	visit (visitor: PROCEDURE [TUPLE [entry: ARCHIVE_ENTRY]])
			-- Open and visit entry snapshots, closing even if `visitor' raises.
			-- The visitor must not advance or close this stream.
		require
			not_started: not is_started
		do
			open
			from until after loop
				visitor.call ([item])
				forth
			end
			close
		rescue
			close
		end

	forth
			-- Skip unread data and read the next header; close on EOF or failure.
		require
			valid_position: not after
		do
			read_next_header
		rescue
			close
		end

	consume
			-- Read and discard the current body, validating declared sizes; close on failure.
		require
			valid_position: not after
		local
			status: INTEGER
		do
			prepare_content
			from until status = End_status or has_failed loop
				status := c_read_block (context)
				check_status (status, Read_error)
				validate_content_block (status)
			end

			if has_failed then
				close
			else
				update_encryption_status
			end
		rescue
			close
		end

	close
			-- Release resources, preserving diagnostics; repeated calls have no effect.
			-- Closing before normal EOF does not complete the traversal.
		do
			release_resources

			is_open := False
			current_entry := Void
		ensure
			closed: not is_open
			no_current_entry: after
		end

feature {NONE} -- Reading

	read_next_header
		local
			status, kind: INTEGER
			name_pointer, target_pointer: POINTER
			name_bytes: STRING_8
			target_bytes: detachable STRING_8
			failure: ARCHIVE_ERROR
		do
			advance_header
			status := header_status

			if status = End_status then
				reached_end := True
			elseif not has_failed then
				name_pointer := c_name (context, False)
				if name_pointer = default_pointer then
					create failure.make (Path_error, "entry pathname",
						"Cannot represent archive entry pathname", path, Void, 0, "none", 0)
					record_failure (failure)
				else
					create name_bytes.make_from_c (name_pointer)
					target_pointer := c_name (context, True)
					if target_pointer /= default_pointer then
						create target_bytes.make_from_c (target_pointer)
					end

					inspect c_kind (context)
					when 1 then
						kind := Regular_file
					when 2 then
						kind := Directory
					when 3 then
						kind := Symbolic_link
					when 4 then
						kind := Hard_link
					else
						kind := Other
					end

					create current_entry.make (name_bytes, kind, c_has_size (context), c_size (context), target_bytes)
					if attached current_entry as entry then
						diagnostic_entry_name := entry.pathname.to_string_32
					end
				end
			end

			if not has_failed then
				capture_metadata
			end

			if reached_end or has_failed then
				close
			end
		end

feature {NONE} -- Metadata snapshots

	capture_metadata
		local
			name_pointer: POINTER
			bytes: STRING_8
			text: IMMUTABLE_STRING_32
			encoding: ARCHIVE_TEXT_ENCODING
			index, count: INTEGER
		do
			create encoding
			name_pointer := c_format_name (context)
			if name_pointer /= default_pointer then
				create bytes.make_from_c (name_pointer)
				create format_name.make_from_string (encoding.text_from_utf_8_bytes (bytes))
			end

			filter_names.wipe_out
			count := c_filter_count (context)
			from until index = count loop
				if c_is_outer_filter (context, index) then
					name_pointer := c_filter_name (context, index)
					if name_pointer = default_pointer then
						create text.make_from_string ("unknown")
					else
						create bytes.make_from_c (name_pointer)
						create text.make_from_string (encoding.text_from_utf_8_bytes (bytes))
					end
					filter_names.extend (text)
				end
				index := index + 1
			end

			update_encryption_status
		end

	update_encryption_status
		do
			inspect c_encryption_status (context)
			when Native_encryption_unsupported then
				encryption_status := Encryption_unsupported
			when 0 then
				encryption_status := Encryption_none_detected
			when 1 then
				encryption_status := Encryption_detected
			else
				encryption_status := Encryption_unknown
			end
		end

feature {NONE} -- Storage

	filter_names: ARRAYED_LIST [IMMUTABLE_STRING_32]

	current_entry: detachable ARCHIVE_ENTRY

	reached_end: BOOLEAN

feature {NONE} -- Native primitives

	c_format_name (a_context: POINTER): POINTER
		external
			"C inline use <arc_read.h>"
		alias
			"return (void *)archive_format_name(((struct arc_read_context *)$a_context)->reader);"
		end

	c_filter_count (a_context: POINTER): INTEGER
		external
			"C inline use <arc_read.h>"
		alias
			"return archive_filter_count(((struct arc_read_context *)$a_context)->reader);"
		end

	c_is_outer_filter (a_context: POINTER; an_index: INTEGER): BOOLEAN
		external
			"C inline use <arc_read.h>"
		alias
			"return archive_filter_code(((struct arc_read_context *)$a_context)->reader, $an_index) != ARCHIVE_FILTER_NONE;"
		end

	c_filter_name (a_context: POINTER; an_index: INTEGER): POINTER
		external
			"C inline use <arc_read.h>"
		alias
			"return (void *)archive_filter_name(((struct arc_read_context *)$a_context)->reader, $an_index);"
		end

	c_encryption_status (a_context: POINTER): INTEGER
		external
			"C inline use <arc_read.h>"
		alias
			"return archive_read_has_encrypted_entries(((struct arc_read_context *)$a_context)->reader);"
		end

	c_read_block (a_context: POINTER): INTEGER
		external
			"C use <arc_read.h>"
		alias
			"arc_read_block"
		end

	c_name (a_context: POINTER; target: BOOLEAN): POINTER
		external
			"C use <arc_read.h>"
		alias
			"arc_read_name"
		end

	c_kind (a_context: POINTER): INTEGER
		external
			"C use <arc_read.h>"
		alias
			"arc_read_kind"
		end

invariant
	entry_requires_open: current_entry /= Void implies is_open
	open_requires_context: is_open implies context /= default_pointer
	complete_requires_end: is_complete implies after

end
