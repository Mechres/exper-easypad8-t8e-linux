#!/usr/bin/env python3
import re
import sys

def parse_gpio(val_str):
    # (gpio:   3, mul: 6, pull -1, drv -1, data -1)
    m = re.search(r"gpio:\s*(\d+),\s*mul:\s*(-?\d+),\s*pull\s*(-?\d+),\s*drv\s*(-?\d+),\s*data\s*(-?\d+)", val_str)
    if not m:
        return val_str
    gpio_num = int(m.group(1))
    mul = m.group(2)
    pull = m.group(3)
    drv = m.group(4)
    data = m.group(5)
    
    bank_idx = gpio_num // 32
    pin_idx = gpio_num % 32
    bank_name = chr(ord('A') + bank_idx)
    
    mul_s = mul if mul != "-1" else "default"
    pull_s = pull if pull != "-1" else "default"
    drv_s = drv if drv != "-1" else "default"
    data_s = data if data != "-1" else "default"
    
    return f"port:P{bank_name}{pin_idx:02d}<{mul_s}><{pull_s}><{drv_s}><{data_s}>"

def parse_sysfs_fex(filepath):
    sections = {}
    current_sec = None
    
    with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line_s = line.strip()
            if not line_s:
                continue
            if line_s.startswith("name:") and not line_s.startswith("name:      name"):
                current_sec = line_s.split("name:", 1)[1].strip()
                if current_sec not in sections:
                    sections[current_sec] = []
            elif current_sec and not line_s.startswith("+") and not line_s.startswith("-") and not line_s.startswith("sub_key:"):
                if len(line) < 16:
                    continue
                content = line[15:]
                matches = list(re.finditer(r"(gpio|int|string|invalid type!)(?:\s{2,}|\s*$)", content))
                valid_m = [m for m in matches if m.start() >= 14]
                if not valid_m:
                    continue
                m = valid_m[0]
                k = content[:m.start()].strip()
                t = m.group(1).strip()
                v = content[m.end():].strip()
                
                if t == "invalid type!":
                    # Unused or uninitialized in FEX, skip or comment out
                    continue
                elif t == "gpio":
                    v_parsed = parse_gpio(v)
                elif t == "string":
                    # Ensure string is quoted
                    if not (v.startswith('"') and v.endswith('"')):
                        v_parsed = f'"{v}"'
                    else:
                        v_parsed = v
                elif t == "int":
                    v_parsed = v
                else:
                    v_parsed = v
                sections[current_sec].append((k, v_parsed))
    return sections

def main():
    sec1 = parse_sysfs_fex("Exper Easypad 8/fex/easypad_t8e_sysfs.fex")
    sec2 = parse_sysfs_fex("Exper Easypad 8/fex/easypad_t8e_extra.fex")
    
    all_secs = {**sec1, **sec2}
    
    out_file = "Exper Easypad 8/fex/easypad_t8e.fex"
    with open(out_file, "w", encoding="utf-8") as out:
        out.write("; Exper Easypad 8 (Model T8E) - Extracted FEX Configuration\n")
        out.write("; Extracted live via sysfs script_dump from real hardware\n\n")
        
        for sec, items in all_secs.items():
            out.write(f"[{sec}]\n")
            for k, v in items:
                out.write(f"{k} = {v}\n")
            out.write("\n")
            
    print(f"Generated clean FEX with {len(all_secs)} sections at: {out_file}")

if __name__ == "__main__":
    main()
