/* Example plugin.
 * Copyright Red Hat
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * * Neither the name of Red Hat nor the names of its contributors may be
 * used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RED HAT AND CONTRIBUTORS ''AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL RED HAT OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

package main

import (
	"C"
	"io/ioutil"
	"libguestfs.org/nbdkit"
	"os"
	"strconv"
	"unsafe"
)

var pluginName = "disk"

// The plugin global struct.
type DiskPlugin struct {
	nbdkit.Plugin
}

// The per-client struct.
type DiskConnection struct {
	nbdkit.Connection
	fd *os.File // Per-client temporary disk.
}

var size uint64
var size_set = false

// Parse the size parameter on the command line.
func (p *DiskPlugin) Config(key string, value string) error {
	if key == "size" {
		var err error
		size, err = strconv.ParseUint(value, 0, 64)
		if err != nil {
			return err
		}
		size_set = true
		return nil
	} else {
		return nbdkit.PluginError{Errmsg: "unknown parameter"}
	}
}

// Make sure the user specified the size parameter.
func (p *DiskPlugin) ConfigComplete() error {
	if !size_set {
		return nbdkit.PluginError{Errmsg: "size parameter is required"}
	}
	return nil
}

func (p *DiskPlugin) Open(readonly bool) (nbdkit.ConnectionInterface, error) {
	// Open a temporary file.
	fd, err := ioutil.TempFile("/var/tmp", "nbdkitdisk")
	if err != nil {
		return nil, err
	}
	os.Remove(fd.Name())

	// Truncate it to the right size.
	err = fd.Truncate(int64(size))
	if err != nil {
		return nil, err
	}

	// Store the file descriptor of the temporary file in the
	// Connection struct.
	return &DiskConnection{fd: fd}, nil
}

func (c *DiskConnection) Close() {
	c.fd.Close()
}

// Return the size of the disk.  We could just return the global
// "size" here, but make the example more interesting.
func (c *DiskConnection) GetSize() (uint64, error) {
	info, err := c.fd.Stat()
	if err != nil {
		return 0, err
	}
	return uint64(info.Size()), nil
}

// Multi-conn is NOT safe because each client sees a different disk.
func (c *DiskConnection) CanMultiConn() (bool, error) {
	return false, nil
}

func (c *DiskConnection) PRead(buf []byte, offset uint64,
	flags uint32) error {
	n, err := c.fd.ReadAt(buf, int64(offset))
	if err != nil {
		return err
	}
	// NBD requests must always read/write the whole requested
	// amount, or else fail.  Actually we should loop here (XXX).
	if n != len(buf) {
		return nbdkit.PluginError{Errmsg: "short read"}
	}
	return nil
}

// Note that CanWrite() is required in golang plugins that implement
// PWrite(), otherwise PWrite() will never be called.
func (c *DiskConnection) CanWrite() (bool, error) {
	return true, nil
}

func (c *DiskConnection) PWrite(buf []byte, offset uint64,
	flags uint32) error {
	n, err := c.fd.WriteAt(buf, int64(offset))
	if err != nil {
		return err
	}
	// NBD requests must always read/write the whole requested
	// amount, or else fail.  Actually we should loop here (XXX).
	if n != len(buf) {
		return nbdkit.PluginError{Errmsg: "short write"}
	}
	return nil
}

// Note that CanFlush() is required in golang plugins that implement
// Flush(), otherwise Flush() will never be called.
func (c *DiskConnection) CanFlush() (bool, error) {
	return true, nil
}

// This is only an example, but if this was a real plugin, because
// these disks are transient and deleted when the client exits, it
// would make no sense to implement a Flush() callback.
func (c *DiskConnection) Flush(flags uint32) error {
	return c.fd.Sync()
}

//----------------------------------------------------------------------
//
// The boilerplate below this line is required by all golang plugins,
// as well as importing "C" and "unsafe" modules at the top of the
// file.

//export plugin_init
func plugin_init() unsafe.Pointer {
	// If your plugin needs to do any initialization, you can
	// either put it here or implement a Load() method.
	// ...

	// Then you must call the following function.
	return nbdkit.PluginInitialize(pluginName, &DiskPlugin{})
}

// This is never(?) called, but must exist.
func main() {}
