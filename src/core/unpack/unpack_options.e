note
	description: "Per-operation password and optional progress observer."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	UNPACK_OPTIONS

create
	make

feature {NONE} -- Initialization

	make
			-- Use no password and no progress notifications.
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

	set_progress_callback (a_callback: PROCEDURE [TUPLE [progress: UNPACK_PROGRESS]])
			-- Observe subsequent operations synchronously through `a_callback'.
		do
			progress_callback := a_callback
		end

	clear_progress_callback
			-- Disable notifications for subsequent operations.
		do
			progress_callback := Void
		end

feature {ARCHIVE_UNPACK_SESSION} -- Operation inputs

	password: detachable IMMUTABLE_STRING_32
			-- Immutable password, absent by default.

	progress_callback: detachable PROCEDURE [TUPLE [progress: UNPACK_PROGRESS]]
			-- Observer retained by reference; absent means no work and no snapshots.

end
