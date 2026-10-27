note
	description: "Immutable native failure snapshot with separately retained cleanup failures."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_ERROR

create {ARC_WRITER, ARC_READER, ARC_ARCHIVE, ARC_ERROR}
	make,
	make_native

feature {NONE} -- Initialization

	make (a_status, a_code: INTEGER; a_message: READABLE_STRING_8)
			-- Copy a diagnostic and initialize an empty cleanup list.
		do
			status := a_status
			code := a_code
			create message.make_from_string (a_message)
			create cleanup_items.make (0)
		ensure
			message_copied: message.same_string (a_message)
			no_cleanup_failures: cleanup_error_count = 0
		end

	make_native (handle: POINTER; a_status: INTEGER)
			-- Snapshot diagnostic bytes before another native operation changes them.
		local
			native: ARC_NATIVE
			message_pointer: POINTER
			native_code: INTEGER
			bytes: STRING_8
		do
			create native.make
			native_code := native.error_code (handle)
			message_pointer := native.error_message (handle)
			if message_pointer /= default_pointer then
				create bytes.make_from_c (message_pointer)
			else
				bytes := "No native diagnostic"
			end
			make (a_status, native_code, bytes)
		end

feature -- Access

	status: INTEGER
			-- Original libarchive operation status.

	code: INTEGER
			-- Original errno-style code, or zero when unavailable.

	message: IMMUTABLE_STRING_8
			-- Copied native message bytes; no password or formatting is added.

	cleanup_error_count: INTEGER
			-- Number of subsequent cleanup failures.
		do
			Result := cleanup_items.count
		end

	cleanup_error (index: INTEGER): ARC_ERROR
			-- Immutable cleanup diagnostic at `index'.
		require
			valid_index: index >= 1 and index <= cleanup_error_count
		do
			Result := cleanup_items [index]
		end

feature {ARC_WRITER, ARC_READER, ARC_ARCHIVE} -- Snapshot construction

	with_cleanup_error (failure: ARC_ERROR): ARC_ERROR
			-- Fresh snapshot preserving this failure and appending `failure'.
		do
			create Result.make (status, code, message)
			across cleanup_items as item loop
				Result.cleanup_items.extend (item)
			end
			Result.cleanup_items.extend (failure)
		ensure
			independent_snapshot: Result /= Current
			primary_status_preserved: Result.status = status
			primary_code_preserved: Result.code = code
			primary_message_preserved: Result.message.same_string (message)
			one_more_failure: Result.cleanup_error_count = cleanup_error_count + 1
			appended_failure: Result.cleanup_error (Result.cleanup_error_count) = failure
		end

feature {ARC_ERROR} -- Storage

	cleanup_items: ARRAYED_LIST [ARC_ERROR]
			-- Privately owned immutable-after-construction diagnostic sequence.

end
