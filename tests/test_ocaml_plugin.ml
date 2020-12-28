let sector_size = 512
let nr_sectors = 2048

let disk = Bytes.make (nr_sectors*sector_size) '\000' (* disk image *)
let sparse = Bytes.make nr_sectors '\000' (* sparseness bitmap *)

(* Test parse_* functions. *)
let () =
  assert (NBDKit.parse_size "1M" = Int64.of_int (1024*1024));
  assert (NBDKit.parse_bool "true" = true);
  assert (NBDKit.parse_bool "0" = false)

(* Test the realpath function. *)
let () =
  let isdir d = try Sys.is_directory d with Sys_error _ -> false in
  let test_dir = "/usr/bin" in
  if isdir test_dir then
    (* We don't know what the answer will be, but it must surely
     * be a directory.
     *)
    assert (isdir (NBDKit.realpath test_dir))

let load () =
  NBDKit.debug "test ocaml plugin loaded"

let unload () =
  (* A good way to find memory bugs: *)
  Gc.compact ();
  NBDKit.debug "test ocaml plugin unloaded"

let params = ref []

let config k v =
  params := (k, v) :: !params

let config_complete () =
  let params = List.rev !params in
  assert (params = [ "a", "1"; "b", "2"; "c", "3" ])

let get_ready () =
  (* We could allocate the disk here, but it's easier to allocate
   * it statically above.
   *)
  NBDKit.debug "test ocaml plugin getting ready"

let open_connection readonly =
  let export_name = NBDKit.export_name () in
  NBDKit.debug "test ocaml plugin handle opened readonly=%b export=%S"
    readonly export_name;
  ()

let close () =
  ()

let list_exports _ _ =
  [ { NBDKit.name = "name1"; description = Some "desc1" };
    { name = "name2"; description = None } ]

let default_export _ _ = "name1"

let get_size () =
  Int64.of_int (Bytes.length disk)

let pread () count offset _ =
  let count = Int32.to_int count in
  let buf = Bytes.create count in
  Bytes.blit disk (Int64.to_int offset) buf 0 count;
  Bytes.unsafe_to_string buf

let set_non_sparse offset len =
  Bytes.fill sparse (offset/sector_size) ((len-1)/sector_size) '\001'

let pwrite () buf offset _ =
  let len = String.length buf in
  let offset = Int64.to_int offset in
  String.blit buf 0 disk offset len;
  set_non_sparse offset len

let extents () count offset _ =
  let extents = Array.init nr_sectors (
    fun sector ->
      { NBDKit.offset = Int64.of_int (sector*sector_size);
        length = Int64.of_int sector_size;
        is_hole = true; is_zero = false }
  ) in
  Bytes.iteri (
    fun i c ->
      if c = '\001' then (* not sparse *)
        extents.(i) <- { extents.(i) with is_hole = false }
  ) sparse;
  Array.to_list extents

let thread_model () =
  NBDKit.THREAD_MODEL_SERIALIZE_CONNECTIONS

let plugin = {
  NBDKit.default_callbacks with
    NBDKit.name     = "testocaml";
    version         = NBDKit.version ();

    load            = Some load;
    unload          = Some unload;
    config          = Some config;
    config_complete = Some config_complete;

    open_connection = Some open_connection;
    close           = Some close;
    list_exports    = Some list_exports;
    default_export  = Some default_export;
    get_size        = Some get_size;
    pread           = Some pread;
    pwrite          = Some pwrite;

    extents         = Some extents;
    thread_model    = Some thread_model;
}

let () = NBDKit.register_plugin plugin
