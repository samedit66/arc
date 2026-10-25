note
	description: "Across adapter sharing the current position of one archive reading stream."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARCHIVE_READ_CURSOR

inherit
	ITERATION_CURSOR [ARCHIVE_ENTRY]

create {ARCHIVE_READ_STREAM}
	make

feature {NONE} -- Initialization

	make (a_stream: ARCHIVE_READ_STREAM)
			-- Borrow the open stream; do not open, rewind or acquire native resources.
		do
			stream := a_stream
		end

feature -- Access

	item: ARCHIVE_ENTRY
			-- Immutable snapshot at the shared stream position.
		do
			Result := stream.item
		end

feature -- Status report

	after: BOOLEAN
			-- Has the stream stopped, failed or closed?
		do
			Result := stream.after
		end

feature -- Cursor movement

	forth
			-- Advance the shared stream, skipping unread entry contents.
		do
			stream.forth
		end

feature {NONE} -- Ownership

	stream: ARCHIVE_READ_STREAM

end
