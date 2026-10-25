note
	description: "Per-operation encoding, password and optional progress observer."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class PACK_OPTIONS

inherit
	ARCHIVE_CONSTANTS

create
	make

feature {NONE} -- Initialization

	make
			-- Infer encoding from the filename, without password or notifications.
		do
			is_filename_encoding := True
		end

feature -- Configuration

	set_encoding (a_format, a_compression: INTEGER)
			-- Use explicit container and outer compression, ignoring the filename suffix.
		require
			known_format: is_valid_format (a_format)
			known_compression: is_valid_compression (a_compression)
		do
			format := a_format
			compression := a_compression
			is_filename_encoding := False
		end

	use_filename_encoding
			-- Infer both container and outer compression from the output suffix.
		do
			is_filename_encoding := True
		end

feature -- Encoding validation

	is_valid_format (value: INTEGER): BOOLEAN
			-- Is `value' a known container identifier?
		do
			Result := value = Zip or value = Tar or value = Seven_zip
		end

	is_valid_compression (value: INTEGER): BOOLEAN
			-- Is `value' a known outer-compression identifier?
		do
			Result := value = None or value = Gzip or value = Bzip2 or value = Xz or value = Zstd
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

	set_progress_callback (a_callback: PROCEDURE [TUPLE [progress: PACK_PROGRESS]])
			-- Observe subsequent operations synchronously through `a_callback'.
		do
			progress_callback := a_callback
		end

	clear_progress_callback
			-- Disable notifications for subsequent operations.
		do
			progress_callback := Void
		end

feature {ARCHIVE_PACK_SESSION} -- Operation inputs

	is_filename_encoding: BOOLEAN
			-- Should the output suffix select the encoding?

	format: INTEGER
			-- Explicit format, meaningful when filename inference is disabled.

	compression: INTEGER
			-- Explicit outer compression, meaningful when filename inference is disabled.

	password: detachable IMMUTABLE_STRING_32
			-- Immutable password, absent by default.

	progress_callback: detachable PROCEDURE [TUPLE [progress: PACK_PROGRESS]]
			-- Observer retained by reference; absent means no work and no snapshots.

end
