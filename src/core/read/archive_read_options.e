note
	description: "Password options captured by an archive reading stream."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_READ_OPTIONS

create
	make

feature {NONE} -- Initialization

	make
			-- Use no password.
		do
		end

feature -- Configuration

	set_password (a_password: READABLE_STRING_GENERAL)
			-- Keep an immutable copy of `a_password' for subsequent operations.
		require
			not_empty: not a_password.is_empty
			without_nul: not a_password.has_code (0)
		do
			create password.make_from_string_general (a_password)
		end

	clear_password
			-- Use no password for subsequent operations.
		do
			password := Void
		end

feature {ARCHIVE_READ_STREAM} -- Operation inputs

	password: detachable IMMUTABLE_STRING_32
			-- Immutable password, absent by default.

end
