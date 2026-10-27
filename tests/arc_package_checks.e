note
	description: "Exercise string profiles, Eiffel recursion, symlinks, exclusive output and failed-output removal."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_PACKAGE_CHECKS

create
	make

feature {NONE} -- Application

	make
			-- Pack and extract the argument tree, then verify ordinary and exceptional cleanup.
		local
			arguments: ARGUMENTS
			writer: ARC_ARCHIVE
			reader: ARC_READER
			source: PATH
			output: PATH
			destination: PATH
			directory: DIRECTORY
			entries, links: INTEGER
			archive_root: STRING_32
		do
			create arguments
			check
				argument_count: arguments.argument_count = 4 or arguments.argument_count = 5
			then
			end
			create source.make_from_string (arguments.argument (1))
			create output.make_from_string (arguments.argument (2))
			create destination.make_from_string (arguments.argument (4))
			if arguments.argument_count = 5 then
				archive_root := arguments.argument (5).as_string_32
			else
				archive_root := "tree"
			end
			create writer.make_open_write (output, arguments.argument (3))
			check
				writer_opened: not writer.has_failed
			then
			end
			writer.set_progress (agent observe)
			writer.add_path (source, archive_root)
			writer.pack
			check
				packed: not writer.has_failed
			then
			end
			writer.close
			check
				committed: not writer.has_failed and not writer.is_open
			then
			end

			create directory.make_with_path (destination)
			directory.create_dir
			create reader.make_open_read (output)
			check
				reader_opened: not reader.has_failed
			then
			end
			from until reader.after loop
				entries := entries + 1
				if reader.has_link_target then
					links := links + 1
					check
						target_copied: not reader.entry_link_target.is_empty
					then
					end
				end
				reader.begin_extraction (destination.extended (reader.entry_name))
				check
					extraction_started: not reader.has_failed
				then
				end
				from until reader.is_entry_complete loop
					reader.extract_block
					check
						extracted_block: not reader.has_failed
					then
					end
				end
				reader.finish_extraction
				check
					extraction_finished: not reader.has_failed
				then
				end
				reader.forth
				check
					advanced: not reader.has_failed
				then
				end
			end
			reader.close
			check
				reader_closed: not reader.has_failed
				tree_entries: (archive_root.is_empty and entries = 8) or (not archive_root.is_empty and entries = 9)
				physical_links: links = 3
				progress_received: notifications > 1
			then
			end

			check_existing_output (output)
			check_existing_output (source)
			check_existing_output (source.extended ("dangling-link"))
			check_cleanup (source, output)
			check_unreadable_directory (source, output)
			check_observer_failure (source, output)
			io.put_string ("PASS: profile " + arguments.argument (3) + "; recursion, links, progress and cleanup%N")
		rescue
			if attached writer as active_writer then
				active_writer.abort
			end
			if attached reader as active_reader then
				active_reader.close
			end
		end

	observe (name: IMMUTABLE_STRING_32; written, total: NATURAL_64)
			-- Inspect immutable per-file progress after successful native block writes.
		do
			check
				named: not name.is_empty
				bounded: written > 0 and written <= total
			then
			end
			notifications := notifications + 1
		end

	check_existing_output (path: PATH)
			-- Reject an existing file, directory or dangling link without acquiring its ownership.
		local
			writer: ARC_ARCHIVE
		do
			create writer.make_open_write (path, "tar")
			check
				rejected: writer.has_failed and not writer.is_open
			then
			end
			writer.close
			writer.abort
			check
				original_remains: object_exists (path)
			then
			end
		end

	check_cleanup (source, output: PATH)
			-- Remove failed, unfinished and explicitly abandoned output while keeping committed output.
		local
			writer: ARC_WRITER
			path: PATH
		do
			create path.make_from_string (output.name + ".failed")
			create writer.make_open_write (path, "tar")
			writer.begin_file (source.extended ("absent.txt"), "absent.txt")
			check
				source_failed: writer.has_failed
			then
			end
			writer.close
			check
				failed_removed: not object_exists (path)
			then
			end

			create path.make_from_string (output.name + ".unfinished")
			create writer.make_open_write (path, "tar")
			writer.begin_file (source.extended ("first.txt"), "first.txt")
			writer.close
			check
				unfinished_removed: writer.has_failed and not object_exists (path)
			then
			end

			create path.make_from_string (output.name + ".aborted")
			create writer.make_open_write (path, "tar")
			writer.abort
			writer.close
			check
				aborted_removed: writer.is_aborted and not object_exists (path)
			then
			end
		rescue
			if attached writer as active_writer then
				active_writer.abort
			end
		end

	check_unreadable_directory (source, output: PATH)
			-- Return directory-open errno through writer state rather than an exception or null DIR access.
		local
			writer: ARC_ARCHIVE
			path, denied: PATH
		do
			denied := source.parent.extended ("denied")
			if object_exists (denied) then
				create path.make_from_string (output.name + ".denied")
				create writer.make_open_write (path, "tar")
				writer.add_path (denied, "denied")
				writer.pack
				check
					io_failed: writer.has_failed
					errno_preserved: writer.last_error.code /= 0
				then
				end
				writer.close
				check
					output_removed: not object_exists (path)
				then
				end
			end
		rescue
			if attached writer as active_writer then
				active_writer.abort
			end
		end

	check_observer_failure (source, output: PATH)
			-- Propagate an observer's IO_FAILURE instead of confusing it with directory I/O.
		local
			writer: detachable ARC_ARCHIVE
			path: PATH
			manager: EXCEPTION_MANAGER
			caught: BOOLEAN
		do
			create path.make_from_string (output.name + ".observer")
			if not caught then
				create writer.make_open_write (path, "tar")
				writer.set_progress (agent raise_observer_failure)
				writer.add_path (source, "tree")
				writer.pack
				check
					observer_must_raise: False
				then
				end
			else
				check
					output_removed: not object_exists (path)
					aborted_writer: attached writer as value and then value.is_aborted
				then
				end
			end
		rescue
			create manager
			if attached manager.last_exception as last and then attached {IO_FAILURE} last.original then
				caught := True
				retry
			end
		end

	raise_observer_failure (name: IMMUTABLE_STRING_32; written, total: NATURAL_64)
			-- Raise a deliberate observer exception to verify cleanup and exception identity.
		local
			failure: IO_FAILURE
		do
			create failure
			failure.raise
		end

	object_exists (path: PATH): BOOLEAN
			-- Does this object itself exist, including a dangling symbolic link?
		local
			info: ARC_PATHS
		do
			create info
			info.classify_source (path)
			Result := info.source_error_code = 0
		end

	notifications: INTEGER
			-- Number of successful data-block notifications observed by the test.

end
