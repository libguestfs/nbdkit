open Unix

let sector_size = 512

let open_connection _ = ()

let get_size () = Int64.of_int (6 * sector_size)

let pread () count offset _ =
  (* Depending on the sector requested (offset), return a different
   * error code.
   *)
  match (Int64.to_int offset) / sector_size with
  | 0 -> (* good, return data *) String.make (Int32.to_int count) '\000'
  | 1 -> NBDKit.set_error EPERM;     failwith "EPERM"
  | 2 -> NBDKit.set_error EIO;       failwith "EIO"
  | 3 -> NBDKit.set_error ENOMEM;    failwith "ENOMEM"
  | 4 -> NBDKit.set_error ESHUTDOWN; failwith "ESHUTDOWN"
  | 5 -> NBDKit.set_error EINVAL;    failwith "EINVAL"
  | _ -> assert false

let plugin = {
  NBDKit.default_callbacks with
    NBDKit.name     = "test-ocaml-errorcodes";
    version         = NBDKit.version ();

    open_connection = Some open_connection;
    get_size        = Some get_size;
    pread           = Some pread;
}

let () = NBDKit.register_plugin plugin
