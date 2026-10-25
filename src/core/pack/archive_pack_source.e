note
	description: "Immutable registered filesystem source and its archive root name."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	ARCHIVE_PACK_SOURCE

create {ARCHIVE_WRITER}
	make

feature {NONE} -- Initialization

	make (a_path: PATH; a_name: READABLE_STRING_GENERAL)
			-- Retain the absolute path and copy the archive name.
		do
			path := a_path.absolute_path
			create entry_name.make_from_string_general (a_name)
		end

feature {ARCHIVE_WRITER, ARCHIVE_PACK_SESSION} -- Access

	path: PATH
			-- Absolute filesystem source.

	entry_name: IMMUTABLE_STRING_32
			-- Stored root name; empty means directory contents at archive root.

end
