# audacity-project-tools

`audacity-project-tools` is a utility application to work with corrupted Audacity projects. 

Internally, the AUP3 file is an SQLite3 database. Such databases are complex binary files that can be corrupted in a few scenarios:
* OS file sync has failed internally;
* Uncommitted WAL file is removed;
* Hardware-level corruption.

Audacity will fail with an error while opening a corrupted project. The common errors are:
* sqlite.rc 11 - database structure is corrupted;
* sqlite.rc 101 - sample block is missing. 

On top of that, there were odd cases, like a half-written project blob.

## Audacity Project Internals

The Audacity 3 project structure is in a binary XML format. Schema is similar to the schema used by previous Audacity versions. After every user action project, Audacity serializes the project as a blob into the `autosave` table. When a user saves the project - Audacity removes the row from the `autosave` table and stores the project to the `project` table. Both tables use the same schema: there are two blobs. One contains "dictionary" data, a list of tag and attribute names; the second holds the project structure. During the startup Audacity checks if the `autosave` table contains any data. If it does, the application shows the dialog that allows the user to "recover" from the `autosave` or discard it.

Audacity stores samples in blocks of up to 1Mb inside the `sampleblocks` table. Each block stores around 5 seconds of mono audio data on the default settings. Audacity never updates blocks. Instead, Audacity creates new blocks when the data changes. For this reason, sample block IDs are not guaranteed to be monotonic or continuous inside a track. If the project structure itself is corrupted, there is no generic way to restore the data.

## Using the `audacity-project-tools`

`audacity-project-tools` is a command-line utility that allows executing a few different commands in the following order:
* `-drop_autosave`: removes an `autosave` table if any. The chances are that dropping this table can help recover a more consistent project.
* `-check_integrity`: performs an integrity check on the database, effectively running `PRAGMA integrity_check;`
* `-extract_project`: extracts the project structure as a text-based XML file from both `autosave` and `project` tables.
* `-recover_db`: attempts to recover the database file using ".recover" command of the `sqlite3` binary. The database will be a correct Audacity project file, passing `-check_integrity`. However, internal consistency is left unchecked. This mode is a must for error code 11 failures.
* `-recover_project`: replaces all the missing blocks with silence. Helps to work with "error code 101" issues.
* `-compact`: removes all the unused blocks and compacts the database.
* `-extract_clips`: extract all the clips as mono wave files. Requires a project to be intact.
* `-extract_sample_blocks`: extract sample blocks as separate wav files. It can be used if the project table is corrupted.
* `-extract_as_mono_track`: extract sample blocks as a single mono wav file.
* `-extract_as_stereo_track`: extract sample blocks as a single stereo wav file. Channels are based on the parity of the block_id.

`audacity-project-tools` will never modify the original file. If mode requires the modification of the database, the tool will create a copy. All the output goes to the same directory as the project file has.

Example:
```
$ audacity-project-tools -recover_db -recover_project broken.aup3`
$ ls 
  broken.aup3 broken.recovered.aup3
```

## Building

`audacity-project-tools` requires a C++17 compliant compiler with the complete C++17 library. The build requires <filesystem> and floating-point versions of `from_chars`.

CMake and Conan are required to configure the project.
