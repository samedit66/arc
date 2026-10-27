note
	description: "Exercise registered archive packing, lifecycle, sequential extraction and callback cleanup."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_ARCHIVE_CHECKS

create
	make

feature {NONE} -- Application

	make
			-- Exercise the high-level facade in a fresh working directory.
		local
			archive: ARC_ARCHIVE
			name: STRING_32
			snapshot: IMMUTABLE_STRING_32
			large: STRING_8
			count: INTEGER
		do
			write_file ("first.txt", "before")
			create archive.make_open_write (path ("registered.zip"), "zip")
			check
				opened_now: archive.is_open_write and exists ("registered.zip")
			then
			end
			name := "first"
			archive.add_path (path ("first.txt"), name)
			name.wipe_out
			archive.add_path (path ("later.txt"), "nested/later")
			check
				registration_without_io: not archive.has_failed and archive.source_count = 2
			then
			end

			write_file ("first.txt", "after registration")
			create large.make_filled ('x', 150000)
			write_file ("later.txt", large)
			active_archive := archive
			archive.set_progress (agent observe)
			archive.pack
			check
				committed: not archive.has_failed and not archive.is_open and not archive.is_aborted
				registrations_retained: archive.source_count = 2
				progress_received: notifications > 1
			then
			end
			archive.close
			archive.abort
			check
				commit_retained: exists ("registered.zip") and not archive.is_aborted
			then
			end

			create archive.make_open_read (path ("registered.zip"))
			check
				first_entry: archive.entry_name.same_string ("first")
				contents_read_during_pack: archive.entry_size = 18
			then
			end
			snapshot := archive.entry_name
			archive.forth
			archive.extract_current (path ("selected.txt"))
			check
				selected_restored: not archive.has_failed and exists ("selected.txt")
				cursor_retained: archive.entry_name.same_string ("nested/later")
				snapshot_retained: snapshot.same_string ("first")
			then
			end
			archive.forth
			check
				ended_but_open: archive.after and archive.is_open_read
			then
			end
			archive.close

			create archive.make_open_read (path ("registered.zip"))
			active_archive := archive
			archive.set_progress (agent observe)
			archive.extract_all (path ("restored"))
			check
				all_restored: not archive.has_failed and archive.after and archive.is_open_read
				physical_destination: attached archive.extraction_directory
				files_restored: exists ("restored/first") and exists ("restored/nested/later")
			then
			end
			archive.close

			create archive.make_open_read (path ("registered.zip"))
			from until archive.after loop
				count := count + 1
				archive.forth
			end
			archive.close
			check
				listed: count = 2 and not archive.has_failed
			then
			end

			check_remaining_operations
			check_inspection_and_passwords
			check_cancellation
			check_failures
			check_empty_archive
			check_extraction_exception
			io.put_string ("PASS: archive registration, lifecycle, extraction and progress%N")
		rescue
			if attached archive as value then
				value.abort
			end
		end

	check_remaining_operations
			-- Check complete, partial and empty passes, retained results and data failure handling.
		local
			archive: ARC_ARCHIVE
			saved: ARC_ARCHIVE_INFO
		do
			create archive.make_open_read (path ("registered.zip"))
			archive.inspect_remaining
			check
				inspected_but_open: not archive.has_failed and archive.after and archive.is_open_read
				counts: attached archive.inspection as summary and then summary.entry_count = 2 and then
					summary.file_count = 2 and then summary.declared_file_bytes = 150018
			end
			check attached archive.inspection as summary then
				saved := summary
			end
			archive.inspect_remaining
			check
				empty_second_pass: attached archive.inspection as summary and then summary.entry_count = 0
				old_result_stable: saved.entry_count = 2 and saved.declared_file_bytes = 150018
			end
			archive.close
			check
				metadata_retained: attached saved.format_name and saved.filter_count > 0
				result_retained: attached archive.inspection
			end

			create archive.make_open_read (path ("registered.zip"))
			archive.forth
			archive.inspect_remaining
			check
				only_remaining_headers: attached archive.inspection as summary and then summary.entry_count = 1 and then
					summary.declared_file_bytes = 150000
			end
			archive.close

			create archive.make_open_read (path ("registered.zip"))
			archive.forth
			archive.verify_remaining
			archive.verify_remaining
			check
				verified_but_open: not archive.has_failed and archive.after and archive.is_open_read and not archive.is_busy
			end
			archive.close

			create archive.make_open_read (path ("bad-header.tar"))
			archive.inspect_remaining
			check
				header_failure: archive.has_failed and archive.after and not archive.is_busy
				no_partial_summary: archive.inspection = Void
			end
			archive.close

			create archive.make_open_read (path ("corrupt.zip"))
			archive.verify_remaining
			check
				data_failure: archive.has_failed and archive.after and not archive.is_busy
			end
			archive.close

			create archive.make_open_read (path ("corrupt.zip"))
			archive.inspect_remaining
			check
				headers_only: not archive.has_failed and attached archive.inspection
			end
			archive.close
		rescue
			if attached archive as active then
				active.close
			end
		end

	check_inspection_and_passwords
			-- Check retained snapshots, body consumption and encrypted creation contracts.
		local
			archive: ARC_ARCHIVE
			reader: ARC_READER
			writer: ARC_WRITER
			secret: STRING_32
			name, filter: IMMUTABLE_STRING_32
			format: detachable IMMUTABLE_STRING_32
		do
			secret := "secret"
			create archive.make_open_write_with_password (path ("encrypted.zip"), "zip", secret)
			secret.wipe_out
			archive.add_path (path ("first.txt"), "first")
			archive.pack
			check
				encrypted_committed: not archive.has_failed
			end

			create reader.make_open_read_with_password (path ("encrypted.zip"), "secret")
			name := reader.entry_name
			format := reader.format_name
			filter := reader.filter_name (1)
			reader.consume_current
			reader.consume_current
			check
				consumed_without_advancing: not reader.has_failed and reader.entry_name = name
				encryption_detected: reader.encryption_status = reader.Encryption_detected
			end
			reader.forth
			reader.close
			check
				ended: reader.after and not reader.has_failed
				format_retained: attached format and then attached reader.format_name as retained_format and then format.same_string (retained_format)
				filter_retained: filter.same_string (reader.filter_name (1))
				name_retained: name.same_string ("first")
			end

			create archive.make_open_read_with_password (path ("encrypted.zip"), "secret")
			archive.verify_remaining
			archive.close
			check
				facade_consumed: archive.after and not archive.has_failed
				facade_summary_retained: attached archive.format_name and archive.filter_count > 0
			end

			create writer.make_open_write_with_password (path ("rejected.7z"), "7z", "secret")
			check
				unsupported_failed: writer.has_failed and not writer.is_open
				no_output: not exists ("rejected.7z")
			end
			writer.close
		end

	check_cancellation
			-- Cancel registered work on close or abort without reading absent sources.
		local
			archive: ARC_ARCHIVE
		do
			create archive.make_open_write (path ("cancelled.tar"), "tar")
			archive.add_path (path ("absent"), "absent")
			archive.close
			archive.close
			check
				cancelled: archive.is_aborted and not archive.has_failed and not exists ("cancelled.tar")
				registration_retained: archive.source_count = 1
			then
			end

			create archive.make_open_write (path ("aborted.zip"), "zip")
			archive.abort
			check
				aborted: archive.is_aborted and not archive.is_open and not exists ("aborted.zip")
			then
			end
		end

	check_failures
			-- Preserve existing output, reject absent sources and retain immutable error snapshots.
		local
			archive: ARC_ARCHIVE
			error: ARC_ERROR
		do
			create archive.make_open_write (path ("registered.zip"), "zip")
			check
				existing_rejected: archive.has_failed and not archive.is_open
			then
			end
			archive.abort
			check
				original_retained: exists ("registered.zip")
			then
			end

			create archive.make_open_write (path ("failed.tar"), "tar")
			archive.add_path (path ("absent"), "absent")
			archive.pack
			check
				failure_closed: archive.has_failed and not archive.is_open
				output_removed: not exists ("failed.tar")
			then
			end
			error := archive.last_error
			archive.close
			check
				immutable_snapshot: not error.message.is_empty
			then
			end

			create archive.make_open_read (path ("registered.zip"))
			archive.extract_all (path ("restored"))
			check
				destination_rejected: archive.has_failed and archive.last_error.code /= 0
			then
			end
			archive.close
		end

	check_empty_archive
			-- Commit an empty registered source set and read its normal EOF.
		local
			archive: ARC_ARCHIVE
		do
			create archive.make_open_write (path ("empty.tar"), "tar")
			archive.pack
			check
				empty_committed: not archive.has_failed and not archive.is_open
			then
			end
			create archive.make_open_read (path ("empty.tar"))
			check
				empty_read: archive.after and not archive.has_failed and archive.is_open_read
			then
			end
			archive.close
		end

	check_extraction_exception
			-- Propagate an observer exception after releasing extraction resources and the activity guard.
		local
			archive: detachable ARC_ARCHIVE
			manager: EXCEPTION_MANAGER
			caught: BOOLEAN
		do
			if not caught then
				create archive.make_open_read (path ("registered.zip"))
				archive.set_progress (agent raise_observer_failure)
				archive.extract_all (path ("interrupted"))
				check
					observer_must_raise: False
				then
				end
			else
				check
					released: attached archive as value and then (not value.is_open and not value.is_busy)
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

	observe (name: IMMUTABLE_STRING_32; written, total: NATURAL_64)
			-- Check successful byte counts and the activity guard during synchronous callbacks.
		do
			check
				busy: attached active_archive as archive and then archive.is_busy
				immutable_name: not name.is_empty
				bounded: written > 0 and written <= total
			then
			end
			notifications := notifications + 1
		end

	raise_observer_failure (name: IMMUTABLE_STRING_32; written, total: NATURAL_64)
			-- Raise a deliberate exception after a successful extraction block.
		local
			error: IO_FAILURE
		do
			create error
			error.raise
		end

	write_file (name, contents: READABLE_STRING_8)
			-- Write fixture contents and release its file on every path.
		local
			file: PLAIN_TEXT_FILE
		do
			create file.make_open_write (name)
			file.put_string (contents)
			file.close
		rescue
			if attached file as value and then not value.is_closed then
				value.close
			end
		end

	path (name: READABLE_STRING_8): PATH
			-- Fresh fixture path relative to the current working directory.
		do
			create Result.make_from_string (name)
		end

	exists (name: READABLE_STRING_8): BOOLEAN
			-- Does a fixture object exist?
		local
			info: FILE_INFO
		do
			create info.make
			info.update (name)
			Result := info.exists
		end

	active_archive: detachable ARC_ARCHIVE
			-- Archive whose active operation the progress callback observes.

	notifications: INTEGER
			-- Number of observed successful block transfers.

end
