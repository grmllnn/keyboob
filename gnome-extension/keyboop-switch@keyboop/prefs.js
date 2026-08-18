import Adw from 'gi://Adw';
import Gdk from 'gi://Gdk';
import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import Gtk from 'gi://Gtk';
import {ExtensionPreferences} from 'resource:///org/gnome/Shell/Extensions/js/extensions/prefs.js';

const DEFAULT_HOTKEY = 'Control+Alt+k';

function configPath() {
  return GLib.build_filenamev(
      [GLib.get_home_dir(), '.config', 'keyboop', 'config']);
}

function readConfig() {
  const cfg = {auto: true, hotkey: DEFAULT_HOTKEY};
  try {
    const file = Gio.File.new_for_path(configPath());
    const [, bytes] = file.load_contents(null);
    const text = new TextDecoder().decode(bytes);
    for (const line of text.split('\n')) {
      if (line.startsWith('auto=')) {
        const v = line.slice(5).trim().toLowerCase();
        cfg.auto = !(v === '0' || v === 'off' || v === 'false' || v === 'no');
      } else if (line.startsWith('hotkey=')) {
        const v = line.slice(7).trim();
        if (v)
          cfg.hotkey = v;
      }
    }
  } catch (e) {
    // missing file → defaults
  }
  return cfg;
}

function writeConfig(cfg) {
  const path = configPath();
  const dir = Gio.File.new_for_path(GLib.path_get_dirname(path));
  try {
    dir.make_directory_with_parents(null);
  } catch (e) {
    // already exists
  }
  const file = Gio.File.new_for_path(path);
  const body = `auto=${cfg.auto ? 'on' : 'off'}\nhotkey=${cfg.hotkey}\n`;
  file.replace_contents(body, null, false,
                        Gio.FileCreateFlags.REPLACE_DESTINATION, null);
}

const MODIFIER_KEYS = new Set([
  'Control_L', 'Control_R', 'Alt_L', 'Alt_R', 'Shift_L', 'Shift_R',
  'Super_L', 'Super_R', 'Meta_L', 'Meta_R', 'ISO_Level3_Shift',
  'Caps_Lock', 'Num_Lock', 'Scroll_Lock',
]);

function accelFromKey(keyval, mask) {
  const name = Gdk.keyval_name(Gdk.keyval_to_lower(keyval));
  if (!name || MODIFIER_KEYS.has(name) || name === 'Escape')
    return null;
  const mods = [];
  if (mask & Gdk.ModifierType.CONTROL_MASK)
    mods.push('Control');
  if (mask & (Gdk.ModifierType.ALT_MASK | Gdk.ModifierType.MOD1_MASK))
    mods.push('Alt');
  if (mask & Gdk.ModifierType.SHIFT_MASK)
    mods.push('Shift');
  if (mask & Gdk.ModifierType.SUPER_MASK)
    mods.push('Super');
  if (!mods.length)
    return null; // need a modifier so typing is not eaten
  mods.push(name);
  return mods.join('+');
}

export default class KeyboopPrefs extends ExtensionPreferences {
  fillPreferencesWindow(window) {
    const cfg = readConfig();
    const page = new Adw.PreferencesPage();
    const group = new Adw.PreferencesGroup();

    const autoRow = new Adw.SwitchRow({
      title: 'Auto convert on space',
      subtitle: 'Manual shortcut still works when off',
    });
    autoRow.active = cfg.auto;
    autoRow.connect('notify::active', () => {
      cfg.auto = autoRow.active;
      writeConfig(cfg);
    });
    group.add(autoRow);

    const hotkeyRow = new Adw.ActionRow({
      title: 'Manual convert shortcut',
      subtitle: 'Click the button, then press a combo (Escape cancels)',
    });
    const btn = new Gtk.Button({
      label: cfg.hotkey,
      valign: Gtk.Align.CENTER,
    });
    btn.add_css_class('flat');
    let recording = false;
    const keys = new Gtk.EventControllerKey();
    btn.add_controller(keys);
    btn.connect('clicked', () => {
      recording = true;
      btn.label = 'Press a shortcut…';
      btn.grab_focus();
    });
    keys.connect('key-pressed', (_c, keyval, _code, state) => {
      if (!recording)
        return false;
      if (keyval === Gdk.KEY_Escape) {
        recording = false;
        btn.label = cfg.hotkey;
        return true;
      }
      const spec = accelFromKey(keyval, state);
      if (!spec)
        return true;
      recording = false;
      cfg.hotkey = spec;
      writeConfig(cfg);
      btn.label = spec;
      return true;
    });
    hotkeyRow.add_suffix(btn);
    hotkeyRow.activatable_widget = btn;
    group.add(hotkeyRow);

    page.add(group);
    window.add(page);
  }
}
