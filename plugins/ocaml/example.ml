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

let ocamlexample_load () =
  (* Debugging output is only printed when the server is in
   * verbose mode (nbdkit -v option).
   *)
  NBDKit.debug "example OCaml plugin loaded"

let ocamlexample_unload () =
  NBDKit.debug "example OCaml plugin unloaded"

let ocamlexample_config key value =
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
let ocamlexample_open readonly =
  let export_name = NBDKit.export_name () in
  NBDKit.debug "example OCaml plugin handle opened readonly=%b export=%S"
    readonly export_name;
  incr id;
  { h_id = !id }

let ocamlexample_get_size h =
  Int64.of_int (Bytes.length !disk)

let ocamlexample_pread h count offset _ =
  let count = Int32.to_int count in
  let buf = Bytes.create count in
  Bytes.blit !disk (Int64.to_int offset) buf 0 count;
  Bytes.unsafe_to_string buf

let ocamlexample_pwrite h buf offset _ =
  let len = String.length buf in
  let offset = Int64.to_int offset in
  String.blit buf 0 !disk offset len

let ocamlexample_thread_model () =
  NBDKit.THREAD_MODEL_SERIALIZE_CONNECTIONS

let plugin = {
  NBDKit.default_callbacks with
    (* name, open_connection, get_size and pread are required,
     * everything else is optional.
     *)
    NBDKit.name        = "ocamlexample";
    version            = "1.0";

    load               = Some ocamlexample_load;
    unload             = Some ocamlexample_unload;

    config             = Some ocamlexample_config;

    open_connection    = Some ocamlexample_open;
    get_size           = Some ocamlexample_get_size;
    pread              = Some ocamlexample_pread;
    pwrite             = Some ocamlexample_pwrite;

    thread_model       = Some ocamlexample_thread_model;
}

let () = NBDKit.register_plugin plugin
