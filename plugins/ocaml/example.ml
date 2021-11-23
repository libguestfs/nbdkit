(* Example OCaml plugin.

   This example can be freely copied and used for any purpose.

   When building nbdkit this example is compiled as:

     plugins/ocaml/nbdkit-ocamlexample-plugin.so

   You can run it from the build directory like this:

     ./nbdkit -f -v plugins/ocaml/nbdkit-ocamlexample-plugin.so [size=100M]

   and connect to it with guestfish like this:

     guestfish --format=raw -a nbd://localhost
     ><fs> run
     ><fs> part-disk /dev/sda mbr
     ><fs> mkfs ext2 /dev/sda1
     ><fs> list-filesystems
     ><fs> mount /dev/sda1 /
     ><fs> [etc]
*)

(* Disk image with default size. *)
let disk = ref (Bytes.make (1024*1024) '\000')

let load () =
  (* Debugging output is only printed when the server is in
   * verbose mode (nbdkit -v option).
   *)
  NBDKit.debug "example OCaml plugin loaded"

let unload () =
  NBDKit.debug "example OCaml plugin unloaded"

(* Add some extra fields to --dump-plugin output.
 * To test this from the build directory:
 *   ./nbdkit --dump-plugin plugins/ocaml/nbdkit-ocamlexample-plugin.so
 *)
let dump_plugin () =
  Printf.printf "ocamlexample_data=42\n";
  flush stdout

let config key value =
  match key with
  | "size" ->
     let size = Int64.to_int (NBDKit.parse_size value) in
     disk := Bytes.make size '\000' (* Reallocate the disk. *)
  | _ ->
     failwith (Printf.sprintf "unknown parameter: %s" key)

(* Any type (even unit) can be used as a per-connection handle.
 * This is just an example.  The same value that you return from
 * your [open_connection] function is passed back as the first
 * parameter to connected functions like get_size and pread.
 *)
type handle = {
  h_id : int; (* just a useless example field *)
}

let id = ref 0
let open_connection readonly =
  let export_name = NBDKit.export_name () in
  NBDKit.debug "example OCaml plugin handle opened readonly=%b export=%S"
    readonly export_name;
  incr id;
  { h_id = !id }

let get_size h =
  NBDKit.debug "example OCaml plugin get_size id=%d" h.h_id;
  Int64.of_int (Bytes.length !disk)

let pread h count offset _ =
  let count = Int32.to_int count in
  let buf = Bytes.create count in
  Bytes.blit !disk (Int64.to_int offset) buf 0 count;
  Bytes.unsafe_to_string buf

let pwrite h buf offset _ =
  let len = String.length buf in
  let offset = Int64.to_int offset in
  String.blit buf 0 !disk offset len

let thread_model () =
  NBDKit.THREAD_MODEL_SERIALIZE_ALL_REQUESTS

let () =
  (* name, open_connection, get_size and pread are required,
   * everything else is optional.
   *)
  NBDKit.register_plugin
    ~name:    "ocamlexample"
    ~version: (NBDKit.version ())
    ~load
    ~unload
    ~dump_plugin
    ~config
    ~open_connection
    ~get_size
    ~pread
    ~pwrite
    ~thread_model
    ()
