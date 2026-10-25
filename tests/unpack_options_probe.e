note
	description: "Integration checks for captured options, progress snapshots and observer failures."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	UNPACK_OPTIONS_PROBE

create
	make

feature {NONE} -- Execution

	make
			-- Extract the supplied archive using the requested observer scenario.
		local
			arguments: ARGUMENTS_32
			value: STRING_32
			failure: ARCHIVE_ERROR
			exceptions: EXCEPTIONS
			previous_locale: POINTER
		do
			create arguments
			create options.make
			create snapshots.make (4)
			mode := arguments.argument (2)
			create archive.make_with_name (arguments.argument (1))

			if arguments.argument_count >= 3 then
				value := arguments.argument (3).to_string_32.twin
				options.set_password (value)
				value.append_string_general (" changed by caller")
			end

			options.set_progress_callback (agent observe)

			previous_locale := c_begin_locale
			caller_locale := c_current_locale
			execute

			check
				caller_locale_restored: c_current_locale = caller_locale
				guard_reset: not archive.is_unpacking
				first_snapshot_unchanged: snapshots.first.written_bytes = 0 and not snapshots.first.is_finished
			end

			c_end_locale (previous_locale)

			if archive.is_successful then
				if mode.same_string ("normal") or mode.same_string ("reenter") then
					check
						final_event: snapshots.last.is_finished
						one_final_event: final_count = 1
					end

					io.put_natural_64 (snapshots.last.written_bytes)
					io.put_character (' ')
					io.put_natural_64 (snapshots.last.completed_entries)
					io.put_new_line
				end
			else
				failure := archive.last_error
				io.error.put_string (failure.stage)

				create exceptions
				exceptions.die (2)
			end
		end

	execute
		local
			directory: DIRECTORY
		do
			if not exception_seen then
				archive.unpack_with_options (options)

				check
					observer_exception_expected: not mode.same_string ("raise") and not mode.same_string ("final_raise")
				end
			elseif mode.same_string ("raise") then
				check
					interrupted: not archive.has_completed_operation and not archive.is_successful
				end

				create directory.make_with_path (archive.unpack_path)
				directory.recursive_delete
				archive.unpack_with_options (options)

				check
					reusable_after_exception: archive.is_successful
				end
			else
				check
					final_success_preserved: archive.is_successful and archive.has_completed_operation
				end
			end
		rescue
			if raised_by_observer and not exception_seen then
				exception_seen := True

				check
					caller_locale_restored_after_exception: c_current_locale = caller_locale
					guard_reset: not archive.is_unpacking
				end

				retry
			end
		end

	observe (progress: UNPACK_PROGRESS)
		local
			failure: DEVELOPER_EXCEPTION
		do
			check
				observer_outside_native_locale: c_current_locale = caller_locale
				guard_active: archive.is_unpacking
				monotonic: snapshots.is_empty or else snapshots.last.written_bytes <= progress.written_bytes
				entries_monotonic: snapshots.is_empty or else snapshots.last.completed_entries <= progress.completed_entries
			end

			snapshots.extend (progress)

			-- These changes must not alter the active operation's captured inputs.
			options.clear_password
			options.clear_progress_callback

			if progress.is_finished then
				final_count := final_count + 1

				check
					result_published: archive.is_successful
					no_current_entry: progress.entry_name = Void
				end
			else
				check
					result_pending: not archive.has_completed_operation
				end
			end

			if mode.same_string ("reenter") and (snapshots.count = 1 or progress.is_finished) then
				verify_reentry_rejected (False)
				verify_reentry_rejected (True)
			end

			if (mode.same_string ("raise") and progress.written_bytes > 0) or
				(mode.same_string ("final_raise") and progress.is_finished)
			then
				raised_by_observer := True
				create failure
				failure.set_description ("probe callback failure")

				failure.raise
			end
		end

	verify_reentry_rejected (verification: BOOLEAN)
			-- Reject extraction and verification during initial and final notifications.
		local
			rejected, returned: BOOLEAN
		do
			if not rejected then
				if verification then
					archive.verify
				else
					archive.unpack
				end
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

	archive: ARCHIVE_READER

	options: UNPACK_OPTIONS

	mode: STRING_32

	snapshots: ARRAYED_LIST [UNPACK_PROGRESS]

	final_count: INTEGER

	raised_by_observer: BOOLEAN

	exception_seen: BOOLEAN

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
