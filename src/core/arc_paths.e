note
	description: "Instance-free lexical checks for native paths and archive entry names."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_PATHS

feature -- Validation

	is_valid_path (path: PATH): BOOLEAN
			-- Is `path' nonempty and free of NUL, without inspecting the filesystem?
		do
			Result := not path.is_empty and not path.name.has ('%U')
		ensure
			instance_free: class
		end

	is_valid_entry_name (name: READABLE_STRING_32): BOOLEAN
			-- Is `name' nonempty and free of NUL, without imposing extraction pathname policy?
		do
			Result := not name.is_empty and not name.has ('%U')
		ensure
			instance_free: class
		end

end
