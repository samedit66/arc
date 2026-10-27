note
	description: "Exercise selected extraction, progress counters, failure snapshots and early cleanup."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_CHECKS

create
	make

feature {NONE} -- Application

	make
			-- Extract selected entries from an argument archive, then exercise error paths.
		local
			arguments: ARGUMENTS
			reader: ARC_READER
			output: PATH
			directory: DIRECTORY
			saved_name: IMMUTABLE_STRING_32
			expected_name: STRING_32
			bytes: NATURAL_64
			blocks: INTEGER
		do
			create arguments
			check
				two_arguments: arguments.argument_count = 2
			then
			end
			create output.make_from_string (arguments.argument (2))
			output := output.absolute_path
			create directory.make_with_path (output)
			directory.create_dir
			create reader.make_open_read (create {PATH}.make_from_string (arguments.argument (1)))
			check
				opened: not reader.has_failed and reader.has_entry
			then
			end
			saved_name := reader.entry_name
			expected_name := saved_name.to_string_32.twin

			from until reader.after loop
				if not reader.entry_name.same_string_general ("docs/first.txt") then
					reader.begin_extraction (output.extended (reader.entry_name))
					check
						extraction_started: not reader.has_failed
					then
					end
					from until reader.is_entry_complete loop
						reader.extract_block
						check
							block_success: not reader.has_failed
						then
						end
						bytes := bytes + reader.last_written_bytes
						if reader.last_written_bytes > 0 then
							blocks := blocks + 1
						end
					end
					reader.finish_extraction
					check
						entry_finished: not reader.has_failed
					then
					end
				end
				reader.forth
				check
					advanced: not reader.has_failed
				then
				end
			end

			reader.close
			reader.close
			check
				closed: not reader.has_failed and not reader.is_open
				copied_name_retained: saved_name.same_string (expected_name)
				transferred_bytes: bytes = 200000
				at_least_one_data_block: blocks > 0
			then
			end
			check_input_ownership
			check_errors
			io.put_string ("PASS: selected extraction, block progress and error snapshots%N")
		rescue
			if attached reader as active_reader then
				active_reader.close
			end
		end

	check_input_ownership
			-- Verify that mutable input names and saved metadata outlive native advancement.
		local
			writer: ARC_WRITER
			reader: ARC_READER
			name: STRING_32
			saved_name: IMMUTABLE_STRING_32
			saved_size: INTEGER_64
		do
			create name.make_from_string ("stable.txt")
			create writer.make_open_write (create {PATH}.make_from_string ("copied-input.tar"), "tar")
			check
				opened: not writer.has_failed
			then
			end

			writer.begin_file (create {PATH}.make_from_string ("second.txt"), name)
			check
				input_not_changed: name.same_string_general ("stable.txt")
				source_opened: not writer.has_failed
			then
			end
			name.wipe_out
			from
			until
				writer.is_entry_complete
			loop
				writer.write_block
				check
					transferred: not writer.has_failed
				then
				end
			end

			writer.finish_entry
			writer.close
			check
				written: not writer.has_failed
			then
			end

			create reader.make_open_read (create {PATH}.make_from_string ("copied-input.tar"))
			check
				positioned: not reader.has_failed and reader.has_entry
				independent_name: reader.entry_name.same_string_general ("stable.txt")
			then
			end
			saved_name := reader.entry_name
			saved_size := reader.entry_size
			reader.forth
			reader.close
			check
				closed: not reader.has_failed
				saved_name_retained: saved_name.same_string_general ("stable.txt")
				saved_size_retained: saved_size = 200000
			then
			end
		rescue
			if attached reader as active_reader then
				active_reader.close
			end
			if attached writer as active_writer then
				active_writer.abort
			end
		end

	check_errors
			-- Verify expected I/O failures, immutable snapshots and unfinished-entry cleanup.
		local
			writer: ARC_WRITER
			reader: ARC_READER
			saved: ARC_ERROR
		do
			create reader.make_open_read (create {PATH}.make_from_string ("does-not-exist.tar"))
			check
				missing_archive: reader.has_failed and not reader.is_open
			then
			end
			saved := reader.last_error
			reader.close
			check
				failure_retained: reader.last_error = saved
			then
			end

			create writer.make_open_write (create {PATH}.make_from_string ("missing-source.tar"), "tar")
			check
				writer_opened: not writer.has_failed
			then
			end
			writer.begin_file (create {PATH}.make_from_string ("does-not-exist.txt"), "missing.txt")
			check
				missing_source: writer.has_failed
			then
			end
			saved := writer.last_error

			writer.close
			check
				immutable_failure: not saved.message.is_empty
			then
			end

			create writer.make_open_write (create {PATH}.make_from_string ("unfinished.tar"), "tar")
			check
				new_writer_opened: not writer.has_failed
			then
			end
			writer.begin_file (create {PATH}.make_from_string ("second.txt"), "second.txt")
			check
				source_opened: not writer.has_failed
			then
			end
			writer.close

			writer.close
			check
				unfinished_detected: writer.has_failed and not writer.has_entry and not writer.is_open
			then
			end
		rescue
			if attached reader as active_reader then
				active_reader.close
			end
			if attached writer as active_writer then
				active_writer.abort
			end
		end

end
