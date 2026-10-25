note
	description: "Assertion-enabled archive creation, options and observer integration probe."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	PACK_PROBE

create
	make

feature {NONE} -- Execution

	make
			-- Pack source/name pairs using the requested scenario and report operation errors.
		local
			args: ARGUMENTS_32
			index: INTEGER
			password, mutable_path, mutable_name: STRING_32
			environment: EXECUTION_ENVIRONMENT
			previous_locale: POINTER
			exceptions: EXCEPTIONS
			encoding: ARCHIVE_TEXT_ENCODING
			late_file: RAW_FILE
		do
			create args
			mode := args.argument (2)
			create writer.make_with_name (args.argument (1))
			create options.make
			create snapshots.make (4)
			from
				index := 3
			until
				index > args.argument_count
			loop
				if mode.same_string ("basename") then
					writer.add_path (create {PATH}.make_from_string (args.argument (index)))
				elseif mode.same_string ("basename_string") then
					writer.add_path_string (args.argument (index))
				elseif mode.same_string ("path_as") then
					writer.add_path_as (create {PATH}.make_from_string (args.argument (index)), args.argument (index + 1))
				elseif mode.same_string ("contents_path") then
					writer.add_directory_contents (create {PATH}.make_from_string (args.argument (index)))
				elseif mode.same_string ("contents_string") or mode.same_string ("contents_late") or mode.same_string ("invalid_name") then
					mutable_path := args.argument (index).twin
					writer.add_directory_contents_string (mutable_path)
					mutable_path.append (" changed by caller")
				elseif mode.same_string ("mutation") then
					mutable_path := args.argument (index).twin
					mutable_name := args.argument (index + 1).twin
					writer.add_path_string_as (mutable_path, mutable_name)
					mutable_path.append (" changed by caller")
					mutable_name.append (" changed by caller")
				else
					writer.add_path_string_as (args.argument (index), args.argument (index + 1))
				end
				index := index + 2
			end

			if mode.same_string ("contents_late") then
				create late_file.make_with_path ((create {PATH}.make_from_string (args.argument (3))).extended ("late.txt"))
				late_file.open_write
				late_file.put_string ("created after registration")
				late_file.close
			end

			if mode.same_string ("cwd") then
				create environment
				environment.change_working_path (writer.path.parent.extended ("исходники📦"))
			end
			check
				parent_rejected: not writer.is_valid_entry_name ("../escape")
				absolute_rejected: not writer.is_valid_entry_name ("/absolute")
				drive_rejected: not writer.is_valid_entry_name ("C:/absolute")
				backslash_rejected: not writer.is_valid_entry_name ("a\b")
				empty_rejected: not writer.is_valid_entry_name ("")
			end
			if mode.same_string ("missing") then
				(create {RAW_FILE}.make_with_name (args.argument (3))).delete
			end
			if mode.same_string ("explicit") or mode.same_string ("reset_encoding") then
				options.set_encoding ({PACK_OPTIONS}.Tar, {PACK_OPTIONS}.Gzip)
			end
			if mode.same_string ("reset_encoding") then
				options.use_filename_encoding
			end
			if mode.same_string ("unsupported") then
				options.set_encoding ({PACK_OPTIONS}.Seven_zip, {PACK_OPTIONS}.Gzip)
			end
			if mode.same_string ("password") or mode.same_string ("password_tar") then
				password := {STRING_32} "secret-%/1044/%/128230/"
				options.set_password (password)
				password.append (" changed by caller")
			end
			options.set_progress_callback (agent observe)

			previous_locale := c_begin_locale
			caller_locale := c_current_locale
			execute
			check
				caller_locale_restored: c_current_locale = caller_locale
			end
			c_end_locale (previous_locale)

			check
				guard_reset: not writer.is_packing
				sources_retained: writer.source_count = (args.argument_count - 2) // 2
				first_snapshot_unchanged: snapshots.is_empty or else (snapshots.first.written_bytes = 0 and not snapshots.first.is_finished)
			end
			if writer.is_successful and not mode.same_string ("plain") then
				check
					final_event: snapshots.last.is_finished
					one_final_event: final_count = 1
				end
				io.put_natural_64 (snapshots.last.written_bytes)
				io.put_character (' ')
				io.put_natural_64 (snapshots.last.completed_entries)
				io.put_new_line
			elseif not writer.is_successful and not exception_seen then
				create encoding
				io.error.put_string (encoding.utf_8_bytes (writer.last_error.message))
				io.error.put_character (' ')
				io.error.put_string (writer.last_error.stage)
				create exceptions
				exceptions.die (2)
			end
		end

	execute
		local
			file: RAW_FILE
			saved_error: ARCHIVE_ERROR
		do
			if not exception_seen then
				if mode.same_string ("plain") then
					writer.pack
				else
					writer.pack_with_options (options)
				end
				if mode.same_string ("invalid_name") then
					check
						path_error: writer.last_error.code = {ARCHIVE_ERROR}.Path_error
						entry_available: attached writer.last_error.entry_name
						no_final_event: final_count = 0
					end
				end
				if mode.same_string ("repeat") then
					writer.pack
					saved_error := writer.last_error
					check
						destination_conflict: saved_error.code = {ARCHIVE_ERROR}.Open_error
					end
					create file.make_with_path (writer.path)
					file.delete
					writer.pack
					check
						recovered: writer.is_successful
						snapshot_unchanged: saved_error.code = {ARCHIVE_ERROR}.Open_error and saved_error.cleanup_error_count = 0
					end
				end
				check
					observer_exception_expected: not mode.same_string ("raise") and not mode.same_string ("final_raise")
				end
			elseif mode.same_string ("raise") then
				check
					interrupted: not writer.has_completed_operation and not writer.is_successful
				end
				create file.make_with_path (writer.path)
				check
					incomplete_removed: not file.exists
				end
				snapshots.wipe_out
				writer.pack_with_options (options)
				check
					reusable: writer.is_successful
				end
			else
				check
					final_success_preserved: writer.is_successful
				end
			end
		rescue
			if raised_by_observer and not exception_seen then
				exception_seen := True
				check
					guard_reset: not writer.is_packing
					locale_restored_after_exception: c_current_locale = caller_locale
				end
				retry
			end
		end

	observe (progress: PACK_PROGRESS)
		local
			failure: DEVELOPER_EXCEPTION
		do
			check
				guard_active: writer.is_packing
				observer_outside_native_locale: c_current_locale = caller_locale
				monotonic: snapshots.is_empty or else snapshots.last.written_bytes <= progress.written_bytes
				entries_monotonic: snapshots.is_empty or else snapshots.last.completed_entries <= progress.completed_entries
			end
			snapshots.extend (progress)
			-- The active session must not observe these changes.
			options.clear_password
			options.set_encoding ({PACK_OPTIONS}.Seven_zip, {PACK_OPTIONS}.Gzip)
			options.clear_progress_callback
			if progress.is_finished then
				final_count := final_count + 1
				check
					result_published: writer.is_successful
				end
			elseif mode.same_string ("reenter") and snapshots.count = 1 then
				verify_reentry_rejected
				verify_registration_rejected
			end
			if not exception_seen and then ((mode.same_string ("raise") and progress.written_bytes > 0) or
				(mode.same_string ("final_raise") and progress.is_finished))
			then
				raised_by_observer := True
				-- The retry uses fresh defaults, demonstrating cleanup and retained sources.
				options.use_filename_encoding
				options.set_progress_callback (agent observe)
				create failure
				failure.set_description ("probe callback failure")
				failure.raise
			end
		end

	verify_reentry_rejected
		local
			rejected, returned: BOOLEAN
		do
			if not rejected then
				writer.pack
				returned := True
			end
			check
				reentry_rejected: rejected and not returned
			end
		rescue
			if not rejected then
				rejected := True
				retry
			end
		end

	verify_registration_rejected
			-- Keep the borrowed source list unchanged during callbacks.
		local
			arguments: ARGUMENTS_32
			rejected, returned: BOOLEAN
		do
			if not rejected then
				create arguments
				writer.add_path_string_as (arguments.argument (3), "reentrant")
				returned := True
			end
			check
				registration_rejected: rejected and not returned
			end
		rescue
			if not rejected then
				rejected := True
				retry
			end
		end

	writer: ARCHIVE_WRITER

	options: PACK_OPTIONS

	mode: STRING_32

	snapshots: ARRAYED_LIST [PACK_PROGRESS]

	final_count: INTEGER

	raised_by_observer, exception_seen: BOOLEAN

	caller_locale: POINTER

feature {NONE} -- Caller locale verification

	c_begin_locale: POINTER
		external
			"C use <probe_locale.h>"
		alias
			"arc_probe_begin_locale"
		end

	c_current_locale: POINTER
		external
			"C use <probe_locale.h>"
		alias
			"arc_probe_current_locale"
		end

	c_end_locale (previous: POINTER)
		external
			"C use <probe_locale.h>"
		alias
			"arc_probe_end_locale"
		end

end
