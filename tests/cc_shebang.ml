(*/.)>/dev/null 2>&1

# The above line is parsed by OCaml as an opening comment and by the
# shell as an impossible command which is ignored.  The line below is
# run by the shell and ignored by OCaml.

exec nbdkit cc "$0" CC=ocamlopt CFLAGS="-output-obj -runtime-variant _pic unix.cmxa NBDKit.cmx -cclib -lnbdkitocaml" "$@"
*)

open Printf

let disk = ref (Bytes.make (1024*1024) '\000')

let config k v =
  match k with
  | "size" ->
     let size = NBDKit.parse_size v in
     let size = Int64.to_int size in
     disk := Bytes.make size '\000'
  | _ ->
     failwith (sprintf "unknown parameter: %s" k)

let open_connection _ = ()

let get_size () = Bytes.length !disk |> Int64.of_int

let pread () count offset _ =
  let count = Int32.to_int count in
  let buf = Bytes.create count in
  Bytes.blit !disk (Int64.to_int offset) buf 0 count;
  Bytes.unsafe_to_string buf

let pwrite () buf offset _ =
  let len = String.length buf in
  let offset = Int64.to_int offset in
  String.blit buf 0 !disk offset len

let () =
  NBDKit.register_plugin
    ~name:    "cc-shebang.ml"
    ~version: (NBDKit.version ())
    ~config
    ~open_connection
    ~get_size
    ~pread
    ~pwrite
    ()
