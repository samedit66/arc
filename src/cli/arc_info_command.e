note
	description: "Declaration of the archive information command."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_INFO_COMMAND

inherit
	CLI_COMMAND

feature -- Definition

	name: STRING_32 = "info"
			-- Command name required by the inherited CLI interface.

	description: STRING_32 = "Show archive information"
			-- Command summary displayed in application help.

feature -- Interface definition

	interface
			-- Declare the options and positional arguments accepted by this command.
		do
			add_optional_string_option ("password", "Archive password", "")
			add_string_argument ("archive", "Archive path")
		end

feature -- Execution

	handle (a_input: CLI_COMMAND_INPUT)
			-- Print metadata and declared file sizes after successful traversal and cleanup.
		local
			stream: detachable ARCHIVE_READ_STREAM
			options: ARCHIVE_READ_OPTIONS
			entry: ARCHIVE_ENTRY
			archive_name, password, diagnostic, filters, encryption: STRING_32
			summary_error: detachable STRING_32
			failure: ARCHIVE_ERROR
			display: ARC_ENTRY_DISPLAY
			entries, files, directories, symlinks, hardlinks, others, declared_bytes, unknown_sizes: NATURAL_64
		do
			archive_name := a_input.string_parameter ("archive")
			if archive_name.is_empty then
				fail ("Archive path must not be empty", Invalid_call)
			end

			create options.make
			password := a_input.string_parameter ("password")
			if not password.is_empty then
				options.set_password (password)
			end
			create display
			create stream.make_with_options (create {PATH}.make_from_string (archive_name), options)

			stream.open
			from until stream.after or summary_error /= Void loop
				entry := stream.item
				entries := entries + 1
				if entry.kind = entry.Regular_file then
					files := files + 1
					if not entry.has_size then
						unknown_sizes := unknown_sizes + 1
					elseif entry.size < 0 then
						summary_error := "Negative declared file size"
					elseif entry.size.to_natural_64 > {NATURAL_64}.Max_value - declared_bytes then
						summary_error := "Declared file sizes exceed the supported total"
					else
						declared_bytes := declared_bytes + entry.size.to_natural_64
					end
				elseif entry.kind = entry.Directory then
					directories := directories + 1
				elseif entry.kind = entry.Symbolic_link then
					symlinks := symlinks + 1
				elseif entry.kind = entry.Hard_link then
					hardlinks := hardlinks + 1
				else
					others := others + 1
				end

				if summary_error = Void then
					stream.forth
				end
			end
			stream.close

			if summary_error /= Void or stream.has_failed then
				diagnostic := {STRING_32} "Cannot inspect " + display.escaped (stream.path.name) + ": "
				if attached summary_error as message then
					diagnostic.append (message)
				end
				if stream.has_failed then
					failure := stream.last_error
					if summary_error /= Void then
						diagnostic.append_string_general ("%NCleanup: ")
					end
					diagnostic.append (display.escaped (failure.message))
					across 1 |..| failure.cleanup_error_count as index loop
						diagnostic.append ({STRING_32} "%NCleanup: " + display.escaped (failure.cleanup_error (index).message))
					end
				end
				fail (diagnostic, 1)
			end

			create filters.make_empty
			across 1 |..| stream.filter_count as index loop
				if not filters.is_empty then
					filters.append_string_general (", ")
				end
				filters.append (display.escaped (stream.filter_name (index)))
			end
			if filters.is_empty then
				filters := "none"
			end
			if stream.encryption_status = stream.Encryption_detected then
				encryption := "yes"
			elseif stream.encryption_status = stream.Encryption_none_detected then
				encryption := "none detected"
			elseif stream.encryption_status = stream.Encryption_unsupported then
				encryption := "unsupported"
			else
				encryption := "unknown"
			end

			display.put_line ({STRING_32} "Archive: " + display.escaped (stream.path.name))
			if attached stream.format_name as format then
				display.put_line ({STRING_32} "Format: " + display.escaped (format))
			else
				display.put_line ("Format: unknown")
			end
			display.put_line ({STRING_32} "Filters: " + filters)
			display.put_line ({STRING_32} "Entries: " + entries.out)
			display.put_line ({STRING_32} "Files: " + files.out)
			display.put_line ({STRING_32} "Directories: " + directories.out)
			display.put_line ({STRING_32} "Symbolic links: " + symlinks.out)
			display.put_line ({STRING_32} "Hard links: " + hardlinks.out)
			display.put_line ({STRING_32} "Other: " + others.out)
			display.put_line ({STRING_32} "Declared file bytes: " + declared_bytes.out)
			display.put_line ({STRING_32} "Files with unknown size: " + unknown_sizes.out)
			display.put_line ({STRING_32} "Encryption: " + encryption)
		rescue
			if attached stream as active_stream then
				active_stream.close
			end
		end

end
