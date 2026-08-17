import Gio from 'gi://Gio';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

let getInputSourceManager;
try {
  ({getInputSourceManager} = await import('resource:///org/gnome/shell/ui/status/keyboard.js'));
} catch (e1) {
  try {
    ({getInputSourceManager} = await import('resource:///org/gnome/shell/ui/keyboard.js'));
  } catch (e2) {
    console.error('KeyboopSwitch: failed to import getInputSourceManager', e2);
  }
}

const IFACE = `
<node>
  <interface name="org.gnome.Shell.Extensions.KeyboopSwitch">
    <method name="Activate">
      <arg type="s" direction="in" name="id"/>
      <arg type="b" direction="out" name="ok"/>
      <arg type="s" direction="out" name="detail"/>
    </method>
  </interface>
</node>`;

class KeyboopSwitchDBus {
  Activate(id) {
    if (!getInputSourceManager)
      return [false, 'getInputSourceManager unavailable'];
    const sources = getInputSourceManager().inputSources;
    for (const k in sources) {
      if (!Object.prototype.hasOwnProperty.call(sources, k))
        continue;
      const src = sources[k];
      if (src.id === id || `${src.type}:${src.id}` === id) {
        src.activate();
        return [true, `${src.type}:${src.id}`];
      }
    }
    const have = [];
    for (const k in sources) {
      if (Object.prototype.hasOwnProperty.call(sources, k))
        have.push(`${sources[k].type}:${sources[k].id}`);
    }
    return [false, `not found ${id}; have ${have.join(',')}`];
  }
}

export default class KeyboopSwitchExtension extends Extension {
  enable() {
    this._impl = new KeyboopSwitchDBus();
    this._dbus = Gio.DBusExportedObject.wrapJSObject(IFACE, this._impl);
    this._dbus.export(Gio.DBus.session,
                      '/org/gnome/Shell/Extensions/KeyboopSwitch');
  }

  disable() {
    this._dbus?.unexport();
    this._dbus = null;
    this._impl = null;
  }
}
