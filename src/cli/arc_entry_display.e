note
	description: "Terminal-safe archive pathname rendering shared by list presentations."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	ARC_ENTRY_DISPLAY

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
		end

	failure_message (action: READABLE_STRING_32; archive_path: PATH; failure: ARCHIVE_ERROR): STRING_32
			-- Terminal-safe operation diagnostic with separate cleanup lines.
		do
			Result := {STRING_32} "Cannot " + escaped (action) + " " + escaped (archive_path.name) + ": " + escaped (failure.message)
			across 1 |..| failure.cleanup_error_count as index loop
				Result.append ({STRING_32} "%NCleanup: " + escaped (failure.cleanup_error (index).message))
			end
		end

	kind_name (entry: ARCHIVE_ENTRY): STRING_32
			-- Human-readable entry type.
		do
			if entry.kind = entry.Regular_file then
				Result := "file"
			elseif entry.kind = entry.Directory then
				Result := "directory"
			elseif entry.kind = entry.Symbolic_link then
				Result := "symlink"
			elseif entry.kind = entry.Hard_link then
				Result := "hardlink"
			else
				Result := "other"
			end
		end

	put_line (text: READABLE_STRING_32)
			-- Write one UTF-8 line to standard output.
		local
			encoding: ARCHIVE_TEXT_ENCODING
		do
			create encoding
			io.put_string (encoding.utf_8_bytes (text))
			io.put_new_line
		end

end
