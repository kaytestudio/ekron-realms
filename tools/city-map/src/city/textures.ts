import * as THREE from "three";

/**
 * Текстура фасада 2x2 окна (каждое окно — свой вариант повреждений):
 * целое, разбитое, наполовину выбитое, выгоревшее. В углу (0.02, 0.02) — глухая стена.
 */
export function createFacadeTexture(): THREE.CanvasTexture {
  const S = 512;
  const B = S / 2;
  const canvas = document.createElement("canvas");
  canvas.width = S;
  canvas.height = S;
  const c = canvas.getContext("2d")!;
  c.fillStyle = "#d8d4cc";
  c.fillRect(0, 0, S, S);

  for (let i = 0; i < 11000; i++) {
    const v = Math.random() < 0.5 ? 0 : 255;
    c.fillStyle = `rgba(${v},${v},${v},${0.03 + Math.random() * 0.05})`;
    c.fillRect(Math.random() * S, Math.random() * S, 1 + Math.random() * 3, 1 + Math.random() * 3);
  }
  // трещины
  c.strokeStyle = "rgba(40,36,32,0.25)";
  c.lineWidth = 1;
  for (let i = 0; i < 14; i++) {
    let x = Math.random() * S;
    let y = Math.random() * S;
    c.beginPath();
    c.moveTo(x, y);
    for (let k = 0; k < 6; k++) {
      x += (Math.random() - 0.5) * 26;
      y += Math.random() * 22;
      c.lineTo(x, y);
    }
    c.stroke();
  }

  for (let by = 0; by < 2; by++) {
    for (let bx = 0; bx < 2; bx++) {
      const variant = bx + by * 2;
      const x = bx * B + B * 0.18;
      const y = by * B + B * 0.16;
      const w = B * 0.64;
      const h = B * 0.62;

      // подоконник и рама
      c.fillStyle = "#b9b5ac";
      c.fillRect(x - 8, y + h, w + 16, 9);
      c.fillStyle = "#6f6d68";
      c.fillRect(x - 5, y - 5, w + 10, h + 10);

      if (variant === 0) {
        const g = c.createLinearGradient(x, y, x + w, y + h);
        g.addColorStop(0, "#4b5e6d");
        g.addColorStop(1, "#141a20");
        c.fillStyle = g;
        c.fillRect(x, y, w, h);
      } else if (variant === 1) {
        c.fillStyle = "#0c0f12";
        c.fillRect(x, y, w, h);
        c.fillStyle = "rgba(130,155,172,0.55)";
        for (let i = 0; i < 7; i++) {
          c.beginPath();
          const px = x + Math.random() * w;
          const py = y + Math.random() * h * (i % 2 ? 0.4 : 1);
          c.moveTo(px, py);
          c.lineTo(px + 8 + Math.random() * 18, py + 4 + Math.random() * 10);
          c.lineTo(px + Math.random() * 12, py + 14 + Math.random() * 22);
          c.closePath();
          c.fill();
        }
      } else if (variant === 2) {
        c.fillStyle = "#050607";
        c.fillRect(x, y, w, h);
        const g = c.createLinearGradient(x, y + h * 0.5, x, y + h);
        g.addColorStop(0, "#35434e");
        g.addColorStop(1, "#10151a");
        c.fillStyle = g;
        c.beginPath();
        c.moveTo(x, y + h);
        c.lineTo(x, y + h * 0.65);
        c.lineTo(x + w * 0.3, y + h * 0.5);
        c.lineTo(x + w * 0.55, y + h * 0.7);
        c.lineTo(x + w * 0.8, y + h * 0.55);
        c.lineTo(x + w, y + h * 0.75);
        c.lineTo(x + w, y + h);
        c.closePath();
        c.fill();
      } else {
        c.fillStyle = "#030303";
        c.fillRect(x, y, w, h);
        // копоть над окном
        const g = c.createLinearGradient(x, y, x, y - B * 0.15);
        g.addColorStop(0, "rgba(18,16,14,0.8)");
        g.addColorStop(1, "rgba(18,16,14,0)");
        c.fillStyle = g;
        c.fillRect(x - 10, y - B * 0.15, w + 20, B * 0.15);
      }
      if (variant !== 3) {
        c.fillStyle = "#6f6d68";
        c.fillRect(x + w / 2 - 2, y, 4, h);
        c.fillRect(x, y + h * 0.45, w, 4);
      }
    }
  }

  const tex = new THREE.CanvasTexture(canvas);
  tex.wrapS = THREE.RepeatWrapping;
  tex.wrapT = THREE.RepeatWrapping;
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 4;
  tex.generateMipmaps = true;
  tex.minFilter = THREE.LinearMipmapLinearFilter;
  tex.needsUpdate = true;
  return tex;
}

export function createMaterials() {
  const map = createFacadeTexture();
  const solid = new THREE.MeshStandardMaterial({ map, vertexColors: true, roughness: 0.95, metalness: 0 });
  solid.name = "Facade";
  const plain = new THREE.MeshStandardMaterial({ vertexColors: true, roughness: 0.97, metalness: 0 });
  plain.name = "Debris";
  const fire = new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide });
  fire.name = "Fire";
  return { solid, plain, fire, map };
}
