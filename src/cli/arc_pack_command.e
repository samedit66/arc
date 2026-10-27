note
	description: "Immediate archive creation through the ARC writer and Eiffel tree packer."
	author: "samedit66"
	email: "samedit66@yandex.ru"

class ARC_PACK_COMMAND

inherit
	CLI_COMMAND

feature -- Definition

	name: STRING_32 = "pack"
			-- Inherited CLI command name.

	description: STRING_32 = "Create an archive"
			-- Application help summary.

feature -- Interface definition

	interface
			-- Declare supported profiles, source mapping and optional progress output.
		do
			add_optional_string_choice_option ("format", "Archive profile; empty selects the filename suffix",
				{ARRAY [STRING_32]} <<"", {ARC_FORMATS}.Tar, {ARC_FORMATS}.Tar_gz,
					{ARC_FORMATS}.Tar_bz2, {ARC_FORMATS}.Tar_xz, {ARC_FORMATS}.Tar_zstd,
					{ARC_FORMATS}.Tar_lz4, {ARC_FORMATS}.Zip, {ARC_FORMATS}.Seven_zip>>,
				Case_sensitive, "")
			add_flag ("keep-root", "Retain the name of a single source directory")
			add_flag ("progress", "Print successful per-file byte transfers to stderr")
			add_optional_string_option ("password", "Archive password", "")
			add_string_argument ("archive", "Archive path")
			add_optional_string_list_argument ("source", "Sources; default to the archive basename directory", {ARRAY [STRING_32]} << >>)
		end

feature -- Execution

	handle (input: CLI_COMMAND_INPUT)
			-- Validate all CLI arguments before creating output, then pack and finalize it.
		local
			writer: ARC_ARCHIVE
			info: ARC_PATHS
			sources: ARRAYED_LIST [STRING_32]
			registrations: ARRAYED_LIST [TUPLE [path: PATH; stored_name: STRING_32]]
			archive_name, source_name, stored_name, explicit_format: STRING_32
			profile, inferred: STRING_8
			output, source_path: PATH
			keep_root, implicit_source: BOOLEAN
			progress: detachable PROCEDURE [TUPLE [name: IMMUTABLE_STRING_32; written, total: NATURAL_64]]
		do
			archive_name := input.string_parameter ("archive")
			if archive_name.is_empty then
				fail ("Archive path must not be empty", Invalid_call)
			end
			create output.make_from_string (archive_name)
			inferred := {ARC_FORMATS}.profile_for_name (archive_name)
			explicit_format := input.string_parameter ("format")
			if explicit_format.is_empty then
				profile := inferred
			else
				profile := explicit_format.to_string_8
				if profile.same_string ("tar") and inferred.starts_with ("tar") then
					profile := inferred
				end
				if not inferred.is_empty and not inferred.same_string (profile) then
					fail ("--format conflicts with the output suffix", Invalid_call)
				end
			end
			if not {ARC_FORMATS}.is_supported_format (profile) then
				fail ("Cannot determine a supported profile; specify --format", Invalid_call)
			end

			sources := input.string_list_parameter ("source").twin
			implicit_source := sources.is_empty
			if implicit_source then
				if attached output.entry as entry then
					source_name := entry.name.to_string_32
					source_name.keep_head (source_name.count - {ARC_FORMATS}.output_suffix (archive_name).count)
				else
					create source_name.make_empty
				end
				if source_name.is_empty or source_name.same_string (".") or source_name.same_string ("..") then
					fail ("Cannot derive a source directory; specify sources explicitly", Invalid_call)
				end
				sources.extend (source_name)
			end
			keep_root := input.boolean_parameter ("keep-root")
			if keep_root and sources.count /= 1 then
				fail ("Keeping the root requires one directory", Invalid_call)
			end
			create registrations.make (sources.count)
			create info
			across sources as source loop
				if source.is_empty then
					fail ("Source path must not be empty", Invalid_call)
				end
				create source_path.make_from_string (source)
				info.classify_source (source_path)
				if info.source_error_code /= 0 then
					fail ({STRING_32} "Source not found: " + {ARC_ENTRY_DISPLAY}.escaped (source), 1)
				end
				if (implicit_source or keep_root) and (not info.is_directory or info.is_symbolic_link) then
					fail ("A real source directory is required", Invalid_call)
				end
				create stored_name.make_empty
				if sources.count /= 1 or keep_root or not info.is_directory or info.is_symbolic_link then
					if attached source_path.canonical_path.entry as entry then
						stored_name := entry.name.to_string_32
					else
						fail ("Source has no archive basename", Invalid_call)
					end
				end
				registrations.extend ([source_path, stored_name])
			end

			if input.string_parameter ("password").is_empty then
				create writer.make_open_write (output, profile)
			else
				create writer.make_open_write_with_password (output, profile, input.string_parameter ("password"))
			end
			if input.boolean_parameter ("progress") then
				progress := agent show_progress
			end
			writer.set_progress (progress)
			across registrations as source loop
				if not writer.has_failed then
					writer.add_path (source.path, source.stored_name)
				end
			end
			if not writer.has_failed then
				writer.pack
			else
				writer.close
			end
			if writer.has_failed then
				fail ({ARC_ENTRY_DISPLAY}.failure_message ("create", output, writer.last_error), 1)
			end
			{ARC_ENTRY_DISPLAY}.put_line ({STRING_32} "Created " + {ARC_ENTRY_DISPLAY}.escaped (archive_name))
		rescue
			if attached writer as active_writer then
				active_writer.abort
			end
		end

feature {NONE} -- Progress

	show_progress (entry_name: IMMUTABLE_STRING_32; written, total: NATURAL_64)
			-- Present successful per-file byte transfers on stderr.
		do
			{ARC_ENTRY_DISPLAY}.put_pack_progress (entry_name, written, total)
		end

end
