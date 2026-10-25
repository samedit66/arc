note
	description: "Assertion-enabled sequential reading, cursor and verification contract probe."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class READ_PROBE

create
	make

feature {NONE} -- Execution

	make
			-- Exercise one valid two-entry archive and one damaged archive supplied by the checker.
		local
			arguments: ARGUMENTS_32
			reader: ARCHIVE_READER
			stream, other: ARCHIVE_READ_STREAM
			options: ARCHIVE_READ_OPTIONS
			first: ARCHIVE_ENTRY
			saved_format: IMMUTABLE_STRING_32
			failure: ARCHIVE_ERROR
			count: INTEGER
		do
			create arguments
			create reader.make_with_name (arguments.argument (1))
			create options.make
			if arguments.argument_count > 3 then
				options.set_password (arguments.argument (4))
			end
			stream := reader.new_stream_with_options (options)
			options.set_password ("wrong-password-after-capture")
			check
				unopened: not stream.is_open and not stream.is_started and stream.after
				no_format: stream.format_name = Void
				no_filters: stream.filter_count = 0
				unknown_encryption: stream.encryption_status = stream.Encryption_unknown
			end

			stream.open
			check opened: stream.is_open and not stream.after end
			check attached stream.format_name as format then
				saved_format := format
			end
			check detected_zip: saved_format.as_upper.starts_with ("ZIP") end
			first := stream.item
			check first_name: first.pathname.same_string_general ("a.txt") end
			stream.consume
			stream.forth
			check second_name: stream.item.pathname.same_string_general ("dir/b.txt") end
			stream.close
			stream.close
			check early_close: stream.after and not stream.is_open and not stream.is_complete and not stream.has_failed end
			check
				retained_name: first.pathname.same_string_general ("a.txt")
				retained_format: attached stream.format_name as format and then format.same_string (saved_format)
				zip_has_no_outer_filter: stream.filter_count = 0
				encryption_detected: (arguments.argument_count > 3) = (stream.encryption_status = stream.Encryption_detected)
			end

			create options.make
			if arguments.argument_count > 3 then
				options.set_password (arguments.argument (4))
			end
			create stream.make_with_options (reader.path, options)
			create other.make_with_options (reader.path, options)
			stream.open
			other.open
			across stream as entry loop
				check same_snapshot_type: not entry.pathname.is_empty end
				count := count + 1
			end
			check independent_position: other.item.pathname.same_string_general ("a.txt") end
			other.close
			stream.close
			check
				complete: stream.is_complete and count = 2
				format_after_eof: attached stream.format_name as format and then format.as_upper.starts_with ("ZIP")
				encryption_after_eof: (arguments.argument_count > 3) = (stream.encryption_status = stream.Encryption_detected)
			end
			check reader_independent: not reader.has_completed_operation end

			create reader.make_with_name (arguments.argument (2))
			reader.verify
			check corrupt_fails: reader.has_completed_operation and not reader.is_successful end
			failure := reader.last_error
			create stream.make (reader.path)
			stream.open
			from until stream.after loop
				stream.consume
				if not stream.after then
					stream.forth
				end
			end
			stream.close
			check stream_error: stream.has_failed and not stream.is_complete end

			create reader.make_with_name (arguments.argument (1))
			reader.verify_with_options (options)
			check verified: reader.is_successful end
			check saved_failure: failure.code = {ARCHIVE_ERROR}.Read_error end
			check no_activity: not reader.is_reading end
			verify_client_exception (reader.path, options)
			verify_visit (reader, options)
			verify_header_failure (reader.path.parent.extended ("bad-header.tar"))
			verify_skip_failure (reader.path.parent.extended ("bad-body.tar"))
			verify_filtered_metadata (create {PATH}.make_from_string (arguments.argument (3)))
		end

	verify_header_failure (a_path: PATH)
			-- Do not blame the preceding entry for corruption in a subsequent header.
		local
			stream: detachable ARCHIVE_READ_STREAM
		do
			create stream.make (a_path)
			stream.open
			check first_entry: stream.item.pathname.same_string_general ("first") end
			stream.consume
			stream.forth
			check
				failed: stream.has_failed and stream.after and not stream.is_open
				header_error: stream.last_error.stage.same_string ("read entry header")
				no_previous_name: stream.last_error.entry_name = Void
			end
		rescue
			if attached stream as active then
				active.close
			end
		end

	verify_skip_failure (a_path: PATH)
			-- Attribute an unread-body failure to its current entry rather than to the next header.
		local
			stream: detachable ARCHIVE_READ_STREAM
		do
			create stream.make (a_path)
			stream.open
			stream.forth
			check
				failed: stream.has_failed and not stream.is_open
				skip_error: stream.last_error.stage.same_string ("skip entry data")
				current_name: attached stream.last_error.entry_name as name and then name.same_string_general ("body")
			end
		rescue
			if attached stream as active then
				active.close
			end
		end

	verify_visit (reader: ARCHIVE_READER; options: ARCHIVE_READ_OPTIONS)
			-- Visit immutable snapshots with scoped cleanup and preserve the reader's published result.
		local
			stream: ARCHIVE_READ_STREAM
		do
			stream := reader.new_stream_with_options (options)
			stream.visit (agent collect_entry)
			check
				visited: visited_entries = 2 and stream.is_complete
				snapshot_retained: attached visited_entry as entry and then entry.pathname.same_string_general ("a.txt")
				reader_result_preserved: reader.is_successful
			end
			verify_visit_exception (reader, options)
		end

	collect_entry (entry: ARCHIVE_ENTRY)
			-- Count visits and retain the first snapshot independently of the stream.
		do
			visited_entries := visited_entries + 1
			if visited_entry = Void then
				visited_entry := entry
			end
		end

	verify_visit_exception (reader: ARCHIVE_READER; options: ARCHIVE_READ_OPTIONS)
			-- Let the scoped visitor release resources before propagating client exceptions.
		local
			stream: detachable ARCHIVE_READ_STREAM
			recovered: BOOLEAN
		do
			if recovered then
				check
					closed: attached stream as active and then (not active.is_open and active.after and not active.is_complete)
					reader_result_preserved: reader.is_successful
				end
			else
				stream := reader.new_stream_with_options (options)
				stream.visit (agent raise_for_entry)
			end
		rescue
			if not recovered then
				recovered := True
				retry
			end
		end

	raise_for_entry (entry: ARCHIVE_ENTRY)
			-- Interrupt a client visitor after observing a valid entry.
		local
			exception: DEVELOPER_EXCEPTION
		do
			check entry_available: not entry.pathname.is_empty end
			create exception
			exception.raise
		end

	visited_entries: INTEGER
			-- Number of entry snapshots visited by the scoped traversal.

	visited_entry: detachable ARCHIVE_ENTRY
			-- Retained first entry snapshot.

	verify_filtered_metadata (a_path: PATH)
			-- Retain immutable outer filter metadata after early close and normal EOF.
		local
			stream: detachable ARCHIVE_READ_STREAM
			filter: IMMUTABLE_STRING_32
		do
			create stream.make (a_path)
			stream.open
			check one_filter: stream.filter_count = 1 end
			filter := stream.filter_name (1)
			stream.close
			stream.close
			check
				filter_copied: filter.same_string_general ("gzip")
				filter_retained: stream.filter_name (1).same_string (filter)
				format_retained: attached stream.format_name
				unsupported_encryption: stream.encryption_status = stream.Encryption_unsupported
			end

			create stream.make (a_path)
			stream.open
			from until stream.after loop
				stream.forth
			end
			check
				complete: stream.is_complete
				filter_after_eof: stream.filter_count = 1 and then stream.filter_name (1).same_string (filter)
			end
		rescue
			if attached stream as active_stream then
				active_stream.close
			end
		end

	verify_client_exception (path: PATH; options: ARCHIVE_READ_OPTIONS)
			-- Close explicitly when processing in an across loop raises an exception.
		local
			stream: detachable ARCHIVE_READ_STREAM
			exception: DEVELOPER_EXCEPTION
			recovered: BOOLEAN
		do
			if recovered then
				check closed: attached stream as active and then (not active.is_open and not active.is_complete) end
			else
				create stream.make_with_options (path, options)
				stream.open
				across stream as entry loop
					create exception
					exception.raise
				end
			end
		rescue
			if not recovered and attached stream as active then
				active.close
				recovered := True
				retry
			end
		end

end
