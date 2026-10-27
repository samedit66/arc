note
	description: "Terminal-safe archive presentation and single-row byte progress on stderr."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_ENTRY_DISPLAY

inherit
	PB_DISPLAY
		rename
			make as make_display,
			put_line as put_progress_line
		redefine
			emit
		end

create
	make_progress

feature {NONE} -- Initialization

	make_progress
			-- Create a single-row progress display writing UTF-8 to stderr.
		do
			make_display
		end

feature -- Formatting

	escaped (text: READABLE_STRING_32): STRING_32
			-- Text with backslashes, control characters and Unicode format controls escaped.
		local
			code: NATURAL_32
		do
			create Result.make (text.count)
			across text as character loop
				code := character.natural_32_code
				if character = '\' then
					Result.append_string_general ("\\")
				elseif code < 32 or (code >= 127 and code <= 159) or
					(code >= 8192 and code <= 8207) or (code >= 8232 and code <= 8239) or
					(code >= 8288 and code <= 8303) or code = 65279
				then
					Result.append_string_general ("\u{")
					Result.append_string_general (code.to_hex_string)
					Result.append_character ('}')
				else
					Result.append_character (character)
				end
			end
		ensure
			instance_free: class
		end

	failure_message (action: READABLE_STRING_32; archive_path: PATH; failure: ARC_ERROR): STRING_32
			-- Terminal-safe operation diagnostic with separate cleanup lines.
		do
			Result := {STRING_32} "Cannot " + escaped (action) + " " + escaped (archive_path.name) + ": " + escaped (decoded (failure.message))
			across 1 |..| failure.cleanup_error_count as index loop
				Result.append ({STRING_32} "%NCleanup: " + escaped (decoded (failure.cleanup_error (index).message)))
			end
		ensure
			instance_free: class
		end

	kind_name (archive: ARC_ARCHIVE): STRING_32
			-- Display label for the current native entry type, retaining ordinary-file labels for hardlinks.
		require
			available: archive.has_entry
		do
			if archive.entry_is_regular then
				Result := "file"
			elseif archive.entry_is_directory then
				Result := "directory"
			elseif archive.entry_is_symbolic_link then
				Result := "symlink"
			else
				Result := "other"
			end
		ensure
			instance_free: class
		end

	decoded (bytes: READABLE_STRING_8): STRING_32
			-- Native diagnostic bytes decoded as UTF-8 with invalid bytes escaped.
		local
			converter: UTF_CONVERTER
		do
			Result := converter.utf_8_string_8_to_escaped_string_32 (bytes)
		ensure
			instance_free: class
		end

	put_line (text: READABLE_STRING_32)
			-- Write one UTF-8 line to standard output.
		local
			converter: UTF_CONVERTER
		do
			io.put_string (converter.utf_32_string_to_utf_8_string_8 (text))
			io.put_new_line
		ensure
			instance_free: class
		end
	put_info (path: PATH; summary: ARC_ARCHIVE_INFO)
			-- Print a terminal-safe inspection summary without reading the archive.
		local
			filters, encryption: STRING_32
		do
			create filters.make_empty
			across 1 |..| summary.filter_count as index loop
				if not filters.is_empty then
					filters.append (", ")
				end
				filters.append (escaped (summary.filter_name (index)))
			end
			if filters.is_empty then
				filters := "none"
			end
			if summary.encryption_status = {ARC_READER}.Encryption_detected then
				encryption := "yes"
			elseif summary.encryption_status = {ARC_READER}.Encryption_none_detected then
				encryption := "none detected"
			elseif summary.encryption_status = {ARC_READER}.Encryption_unsupported then
				encryption := "unsupported"
			else
				encryption := "unknown"
			end
			put_line ({STRING_32} "Archive: " + escaped (path.name))
			if attached summary.format_name as format then
				put_line ({STRING_32} "Format: " + escaped (format))
			else
				put_line ("Format: unknown")
			end
			put_line ({STRING_32} "Filters: " + filters)
			put_line ({STRING_32} "Entries: " + summary.entry_count.out)
			put_line ({STRING_32} "Files: " + summary.file_count.out)
			put_line ({STRING_32} "Directories: " + summary.directory_count.out)
			put_line ({STRING_32} "Symbolic links: " + summary.symbolic_link_count.out)
			put_line ({STRING_32} "Hard links: " + summary.hardlink_count.out)
			put_line ({STRING_32} "Other: " + summary.other_count.out)
			put_line ({STRING_32} "Declared file bytes: " + summary.declared_file_bytes.out)
			put_line ({STRING_32} "Files with unknown size: " + summary.unknown_size_count.out)
			put_line ({STRING_32} "Encryption: " + encryption)
		ensure
			instance_free: class
		end

feature -- Progress

	update_progress (entry_name: IMMUTABLE_STRING_32; written, total: NATURAL_64)
			-- Show transferred bytes for the current file, with a spinner when its size is unknown.
		local
			current_bar: PB_PROGRESS_BAR
			formatter: PB_FORMATTER
			count, step: INTEGER_64
			label: STRING_32
		do
			count := written.min ({INTEGER_64}.max_value.to_natural_64).to_integer_64
			if not attached progress_name as previous or else not previous.same_string (entry_name) or else
				written < last_written or else total /= last_total
			then
				make_display
				if total > 0 and total <= {INTEGER_64}.max_value.to_natural_64 then
					create current_bar.make_with_total (total.to_integer_64)
				else
					create current_bar.make_unknown
				end
				formatter := {PB_FORMATTER_PRESETS}.unicode
				formatter.set_width (24)
				formatter.set_show_timing (False)
				formatter.set_show_count (True)
				current_bar.set_formatter (formatter)
				progress_bar := current_bar
				progress_name := entry_name
				last_step := -1
				extend (current_bar)
			end

			last_written := written
			last_total := total

			check attached progress_bar as bar then
				bar.set_absolute_progress (count.min (bar.progress_limit))
				if bar.has_total then
					step := bar.progress_percentage
				else
					step := count // 65536
				end
				if step /= last_step then
					label := escaped (entry_name)
					if label.count > 32 then
						label.keep_head (29)
						label.append ("...")
					end
					bar.set_description ({STRING_32} "Progress: " + label + " (bytes)")
					bar.render
					last_step := step
				end
			end
		end

feature {NONE} -- Progress output

	emit (text: READABLE_STRING_GENERAL)
			-- Write the library's terminal rendering to stderr without touching stdout.
		local
			converter: UTF_CONVERTER
		do
			io.error.put_string (converter.utf_32_string_to_utf_8_string_8 (text.as_string_32))
			io.error.flush
		end

	progress_bar: detachable PB_PROGRESS_BAR
			-- Current file's progress bar.

	progress_name: detachable IMMUTABLE_STRING_32
			-- Current file's immutable archive name.

	last_written: NATURAL_64
			-- Previous callback byte count, used to recognize repeated entry names.

	last_total: NATURAL_64
			-- Previous callback declared size.

	last_step: INTEGER_64
			-- Last rendered percentage or unknown-size block.

end
