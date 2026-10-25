note
	description: "UTF-8 text boundary shared by archive names, passwords and diagnostics."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	ARCHIVE_TEXT_ENCODING

feature -- Conversion

	utf_8_bytes (a_text: READABLE_STRING_GENERAL): STRING_8
			-- Fresh UTF-8 bytes of Unicode text; filesystem paths use PATH instead.
		local
			converter: UTF_CONVERTER
		do
			Result := converter.utf_32_string_to_utf_8_string_8 (a_text)
		end

	text_from_utf_8_bytes (a_bytes: READABLE_STRING_8): STRING_32
			-- Fresh text with invalid UTF-8 bytes preserved through ELKS escapes.
		local
			converter: UTF_CONVERTER
		do
			-- Native diagnostics may contain bytes outside UTF-8; never discard them.
			Result := converter.utf_8_string_8_to_escaped_string_32 (a_bytes)
		end

end
