note
	description: "Shared archive-name policy for registered sources and discovered packing entries."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	ARCHIVE_PACK_NAMES

feature {ARCHIVE_WRITER, ARCHIVE_PACK_SESSION} -- Names

	is_valid_name (a_name: READABLE_STRING_GENERAL): BOOLEAN
			-- Is `a_name' empty or a relative archive name with no NUL, backslash or parent component?
		local
			name, normalized: STRING_32
		do
			name := a_name.as_string_32
			Result := not name.has ('%U') and not name.has ('\') and then
				(name.is_empty or else name [1] /= '/')
			across name.split ('/') as component loop
				Result := Result and not component.same_string ("..")
			end
			normalized := normalized_name (name)
			Result := Result and then (name.is_empty or else not normalized.is_empty) and then
				not (normalized.count >= 2 and then normalized [2] = ':')
		end

	normalized_name (a_name: READABLE_STRING_GENERAL): STRING_32
			-- Fresh archive name with redundant separators and current components removed.
		do
			create Result.make (a_name.count)
			across a_name.as_string_32.split ('/') as component loop
				if not component.is_empty and not component.same_string (".") then
					if not Result.is_empty then
						Result.append_character ('/')
					end
					Result.append (component)
				end
			end
		end

end
