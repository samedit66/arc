note
	description: "Immutable metadata copied from one archive entry, independent of its stream."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_ENTRY

inherit
	ARCHIVE_CONSTANTS

create {ARCHIVE_READ_STREAM}
	make

feature {NONE} -- Initialization

	make (a_name: READABLE_STRING_8; a_kind: INTEGER; a_has_size: BOOLEAN;
		a_size: INTEGER_64; a_target: detachable READABLE_STRING_8)
			-- Copy pathname and link bytes and decode them without discarding invalid UTF-8.
		local
			encoding: ARCHIVE_TEXT_ENCODING
		do
			create encoding
			create raw_pathname.make_from_string (a_name)
			create pathname.make_from_string (encoding.text_from_utf_8_bytes (a_name))
			kind := a_kind
			has_size := a_has_size
			stored_size := a_size

			if attached a_target as target then
				create raw_link_target.make_from_string (target)
				create link_target.make_from_string (encoding.text_from_utf_8_bytes (target))
			end
		end

feature -- Access

	pathname: IMMUTABLE_STRING_32
			-- Stored pathname decoded as UTF-8 with lossless ELKS escapes; never normalized.

	raw_pathname: IMMUTABLE_STRING_8
			-- Copied UTF-8 pathname bytes, or native name bytes if UTF-8 conversion was unavailable.

	kind: INTEGER
			-- Symbolic entry type, independent of native ABI values.

	has_size: BOOLEAN
			-- Is a declared uncompressed size available?

	size: INTEGER_64
			-- Declared uncompressed size in bytes.
		require
			size_available: has_size
		do
			Result := stored_size
		end

	link_target: detachable IMMUTABLE_STRING_32
			-- Stored symbolic-link or hardlink target, decoded without path normalization.

	raw_link_target: detachable IMMUTABLE_STRING_8
			-- Copied target bytes, when present.

feature {NONE} -- Storage

	stored_size: INTEGER_64

end
