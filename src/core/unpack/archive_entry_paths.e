note
	description: "Pure pathname policy for extraction, independent of native resource ownership."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	ARCHIVE_ENTRY_PATHS

feature -- Validation

	is_valid_name (a_name: READABLE_STRING_32): BOOLEAN
			-- Can `a_name' be extracted as a relative name on this platform?
		do
			Result := is_valid_relative_name (a_name, {PLATFORM}.is_windows)
		end

	is_valid_relative_name (a_name: READABLE_STRING_32; windows: BOOLEAN): BOOLEAN
			-- Validate the original name before PATH can normalize it.
		local
			name: STRING_32
		do
			name := a_name.to_string_32.twin
			if windows then
				name.replace_substring_all ("\", "/")
			end

			Result := not name.is_empty and then name [1] /= '/' and then not name.has ('%U')

			across name.split ('/') as component loop
				if component.same_string ("..") then
					Result := False
				elseif windows and then not component.is_empty and then not component.same_string (".") then
					Result := Result and is_valid_windows_component (component)
				end
			end
		end

feature {NONE} -- Windows policy

	is_valid_windows_component (a_component: STRING_32): BOOLEAN
		local
			base: STRING_32
			dot: INTEGER
			digit: CHARACTER_32
		do
			Result := not a_component.ends_with (".") and not a_component.ends_with (" ")

			across a_component as character loop
				if character.code < 32 or else ({STRING_32} ":*?%"<>|").has (character) then
					Result := False
				end
			end

			base := a_component.as_upper
			dot := base.index_of ('.', 1)
			if dot > 0 then
				base.keep_head (dot - 1)
			end

			if base.same_string ("CON") or base.same_string ("PRN") or base.same_string ("AUX") or base.same_string ("NUL") then
				Result := False
			elseif base.count = 4 and then (base.starts_with ("COM") or base.starts_with ("LPT")) then
				digit := base [4]
				if (digit >= '1' and digit <= '9') or digit.code = 185 or digit.code = 178 or digit.code = 179 then
					Result := False
				end
			end
		end

end
