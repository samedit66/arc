note
	description: "Assertion-enabled archive extraction and operation-result probe."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class
	UNPACK_PROBE

create
	make

feature {NONE} -- Execution

	make
			-- Run result-contract checks and extract the archive supplied on the command line.
		local
			arguments: ARGUMENTS_32
			archive, named_archive: ARCHIVE_READER
			writer: ARCHIVE_WRITER
			saved_error: detachable ARCHIVE_ERROR
			failure: ARCHIVE_ERROR
			directory: DIRECTORY
			index: INTEGER
			encoding: ARCHIVE_TEXT_ENCODING
			exceptions: EXCEPTIONS
		do
			verify_path_policy
			verify_text_encoding
			create encoding

			create arguments
			create archive.make_with_path (create {PATH}.make_from_string (arguments.argument (1)))
			create named_archive.make_with_name (arguments.argument (1))
			create writer.make_with_path (archive.path)

			check
				same_source: archive.path.same_as (named_archive.path)
				same_writer_path: writer.path.same_as (archive.path)
				same_destination: archive.unpack_path.same_as (named_archive.unpack_path)
				initially_pending: not archive.has_completed_operation
				initially_unsuccessful: not archive.is_successful
			end

			verify_unavailable_error (archive)
			verify_unavailable_error (writer)

			if arguments.argument_count = 2 then
				from
					index := 1
				until
					index > 20
				loop
					archive.unpack

					check
						failed_normally: archive.has_completed_operation and not archive.is_successful
						has_error: archive.last_error.code = {ARCHIVE_ERROR}.Open_error
					end

					index := index + 1
				end

				create archive.make_with_name (arguments.argument (2))
				archive.unpack

				check
					successful: archive.is_successful
				end

				archive.unpack
				saved_error := archive.last_error

				check
					failed_after_success: not archive.is_successful and archive.has_completed_operation
					destination_error: attached saved_error as error and then error.code = {ARCHIVE_ERROR}.Destination_error
				end

				create directory.make_with_path (archive.unpack_path)
				directory.recursive_delete
			end

			archive.unpack

			check
				completed: archive.has_completed_operation
				independent_writer_result: not writer.has_completed_operation and not writer.is_successful
			end

			if attached saved_error as error then
				check
					recovered: archive.is_successful
					old_snapshot_unchanged: error.code = {ARCHIVE_ERROR}.Destination_error and error.cleanup_error_count = 0
				end
			end

			if archive.is_successful then
				verify_unavailable_error (archive)
			else
				failure := archive.last_error
				io.error.put_string (failure.stage)
				io.error.put_string (": ")
				io.error.put_string (encoding.utf_8_bytes (failure.message))
				io.error.put_new_line

				create exceptions
				exceptions.die (2)
			end
		end

	verify_text_encoding
			-- Verify Unicode bytes and lossless malformed diagnostics at the text boundary.
		local
			encoding: ARCHIVE_TEXT_ENCODING
			bytes: STRING_8
			diagnostic: STRING_32
		do
			create encoding
			bytes := "a%/255/b%/195/"
			diagnostic := encoding.text_from_utf_8_bytes (bytes)

			check
				unicode_bytes: encoding.utf_8_bytes ({STRING_32} "%/1044/%/128230/").same_string (
					"%/208/%/148/%/240/%/159/%/147/%/166/")
				unicode_text: encoding.text_from_utf_8_bytes ("%/208/%/148/%/240/%/159/%/147/%/166/").same_string (
					{STRING_32} "%/1044/%/128230/")
				malformed_preserved: diagnostic.same_string ({STRING_32} "a%/65533/FFb%/65533/C3")
				input_unchanged: bytes.same_string ("a%/255/b%/195/")
			end
		end

	verify_unavailable_error (archive: ARCHIVE)
			-- Verify that pending and successful operations reject error access.
		local
			rejected, access_returned: BOOLEAN
			unexpected: detachable ARCHIVE_ERROR
		do
			if not rejected then
				unexpected := archive.last_error
				access_returned := True
			end

			check
				access_rejected: rejected and not access_returned
			end
		rescue
			if not rejected then
				rejected := True
				retry
			end
		end

	verify_path_policy
		local
			policy: ARCHIVE_ENTRY_PATHS
		do
			create policy

			check
				valid_windows: policy.is_valid_relative_name ("directory/file.txt", True)
				valid_number: policy.is_valid_relative_name ("COM10.txt", True)
				reserved: not policy.is_valid_relative_name ("CON.txt", True)
				reserved_port: not policy.is_valid_relative_name ("LPT1", True)
				invalid_character: not policy.is_valid_relative_name ("file?", True)
				alternate_stream: not policy.is_valid_relative_name ("file:stream", True)
				trailing_dot: not policy.is_valid_relative_name ("file.", True)
				trailing_space: not policy.is_valid_relative_name ("file ", True)
				absolute_drive: not policy.is_valid_relative_name ("C:\file", True)
				unc_path: not policy.is_valid_relative_name ("\\server\file", True)
				parent_component: not policy.is_valid_relative_name ("dir\..\escape", True)
				posix_backslash: policy.is_valid_relative_name ("file\name", False)
			end
		end

end
