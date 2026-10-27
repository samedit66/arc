note
	description: "Create three files in each supported archive format and print every entry."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_DEMO

create
	make

feature {NONE} -- Application

	make
			-- Create fixtures, write eight archives, then list their entries in a loop.
		local
			large: STRING_8
		do
			write_source ("first.txt", "first file%N")
			create large.make_filled ('x', 200000)
			write_source ("second.txt", large)
			write_source ("third.txt", "")

			run_format ("example.tar", "tar")
			run_format ("example.tar.gz", "tar.gz")
			run_format ("example.tar.bz2", "tar.bz2")
			run_format ("example.tar.xz", "tar.xz")
			run_format ("example.tar.zst", "tar.zst")
			run_format ("example.tar.lz4", "tar.lz4")
			run_format ("example.zip", "zip")
			run_format ("example.7z", "7z")
		end

	run_format (filename: READABLE_STRING_8; format: READABLE_STRING_8)
			-- Create one directory and three files, then print copied entry metadata.
		local
			writer: ARC_WRITER
			reader: ARC_READER
			count: INTEGER
			converter: UTF_CONVERTER
		do
			create writer.make_open_write (create {PATH}.make_from_string (filename), format)
			require_writer_success (writer)
			writer.begin_directory ("docs")
			require_writer_success (writer)
			writer.finish_entry
			require_writer_success (writer)

			add_file (writer, "first.txt", "docs/first.txt")
			add_file (writer, "second.txt", "docs/second.txt")
			add_file (writer, "third.txt", "docs/%/1087/%/1088/%/1080/%/1084/%/1077/%/1088/.txt")
			writer.close
			require_writer_success (writer)

			io.put_string ("Archive: " + filename.to_string_8 + "%N")
			create reader.make_open_read (create {PATH}.make_from_string (filename))
			require_reader_success (reader)
			from until reader.after loop
				io.put_string ("  " + converter.utf_32_string_to_utf_8_string_8 (reader.entry_name))
				io.put_string (" type=")
				io.put_integer (reader.entry_type)
				if reader.has_entry_size then
					io.put_string (" size=")
					io.put_integer_64 (reader.entry_size)
				end
				io.put_new_line
				count := count + 1
				reader.forth
				require_reader_success (reader)
			end
			reader.close
			require_reader_success (reader)
			check
				directory_and_three_files: count = 4
			then
			end
		rescue
			if attached writer as active_writer then
				active_writer.abort
			end
			if attached reader as active_reader then
				active_reader.close
			end
		end

	add_file (writer: ARC_WRITER; source: READABLE_STRING_8; name: READABLE_STRING_32)
			-- Transfer a file in blocks and verify the successful-byte total.
		local
			total: NATURAL_64
			blocks: INTEGER
		do
			writer.begin_file (create {PATH}.make_from_string (source), name)
			require_writer_success (writer)
			from until writer.is_entry_complete loop
				writer.write_block
				require_writer_success (writer)
				total := total + writer.last_written_bytes
				if writer.last_written_bytes > 0 then
					blocks := blocks + 1
				end
				-- A CLI progress observer can be called here, outside native code.
			end
			check
				size_matches: total = writer.current_size.to_natural_64
			then
			end
			if writer.current_size > 65536 then
				check
					multiple_source_blocks: blocks > 1
				then
				end
			end
			writer.finish_entry
			require_writer_success (writer)
		end

	write_source (name, content: READABLE_STRING_8)
			-- Write one caller-owned test fixture in the working directory.
		local
			file: PLAIN_TEXT_FILE
		do
			create file.make_with_name (name)
			file.open_write
			file.put_string (content)
			file.close
		rescue
			if attached file as active_file and then not active_file.is_closed then
				active_file.close
			end
		end

	require_writer_success (writer: ARC_WRITER)
			-- Print an operation diagnostic before asserting test success.
		do
			if writer.has_failed then
				io.error.put_string (writer.last_error.message.to_string_8 + "%N")
			end
			check
				successful_writer: not writer.has_failed
			then
			end
		end

	require_reader_success (reader: ARC_READER)
			-- Print an operation diagnostic before asserting test success.
		do
			if reader.has_failed then
				io.error.put_string (reader.last_error.message.to_string_8 + "%N")
			end
			check
				successful_reader: not reader.has_failed
			then
			end
		end

end
