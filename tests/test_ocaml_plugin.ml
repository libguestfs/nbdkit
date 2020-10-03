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

let test_load () =
  NBDKit.debug "test ocaml plugin loaded"

let test_unload () =
  NBDKit.debug "test ocaml plugin unloaded";
  (* A good way to find memory bugs: *)
  Gc.compact ()

let params = ref []

let test_config k v =
  params := (k, v) :: !params

let test_config_complete () =
  let params = List.rev !params in
  assert (params = [ "a", "1"; "b", "2"; "c", "3" ])

let test_get_ready () =
  (* We could allocate the disk here, but it's easier to allocate
   * it statically above.
   *)
  NBDKit.debug "test ocaml plugin getting ready"

let test_open readonly =
  let export_name = NBDKit.export_name () in
  NBDKit.debug "test ocaml plugin handle opened readonly=%b export=%S"
    readonly export_name;
  ()

let test_close () =
  ()

let test_list_exports _ _ =
  [ { NBDKit.name = "name1"; description = Some "desc1" };
    { name = "name2"; description = None } ]

let test_default_export _ _ = "name1"

let test_get_size () =
  Int64.of_int (Bytes.length disk)

let test_pread () count offset _ =
  let count = Int32.to_int count in
  let buf = Bytes.create count in
  Bytes.blit disk (Int64.to_int offset) buf 0 count;
  Bytes.unsafe_to_string buf

let set_non_sparse offset len =
  Bytes.fill sparse (offset/sector_size) ((len-1)/sector_size) '\001'

let test_pwrite () buf offset _ =
  let len = String.length buf in
  let offset = Int64.to_int offset in
  String.blit buf 0 disk offset len;
  set_non_sparse offset len

let test_extents () count offset _ =
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

let plugin = {
  NBDKit.default_callbacks with
    NBDKit.name     = "testocaml";
    version         = "1.0";

    load            = Some test_load;
    unload          = Some test_unload;
    config          = Some test_config;
    config_complete = Some test_config_complete;

    open_connection = Some test_open;
    close           = Some test_close;
    list_exports    = Some test_list_exports;
    default_export  = Some test_default_export;
    get_size        = Some test_get_size;
    pread           = Some test_pread;
    pwrite          = Some test_pwrite;

    extents         = Some test_extents;
    thread_model    = Some (fun () -> NBDKit.THREAD_MODEL_SERIALIZE_CONNECTIONS);
}

let () = NBDKit.register_plugin plugin
