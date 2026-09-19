// Solve the 3x3 projective transform taking the rectangle (0,0)-(w,h) onto four
// destination points, and express it as a CSS matrix3d. This is what lets a
// screen authored in device pixels sit inside a photograph of the device.
export type Pt = [number, number];

const solve = (A: number[][], b: number[]): number[] => {
  const n = b.length;
  const M = A.map((row, i) => [...row, b[i]]);
  for (let c = 0; c < n; c++) {
    let p = c;
    for (let r = c + 1; r < n; r++) if (Math.abs(M[r][c]) > Math.abs(M[p][c])) p = r;
    [M[c], M[p]] = [M[p], M[c]];
    for (let r = 0; r < n; r++) {
      if (r === c) continue;
      const f = M[r][c] / M[c][c];
      for (let k = c; k <= n; k++) M[r][k] -= f * M[c][k];
    }
  }
  return M.map((row, i) => row[n] / row[i]);
};

export const matrix3d = (w: number, h: number, dst: [Pt, Pt, Pt, Pt]): string => {
  const src: Pt[] = [[0, 0], [w, 0], [w, h], [0, h]];
  const A: number[][] = [];
  const b: number[] = [];
  src.forEach(([x, y], i) => {
    const [u, v] = dst[i];
    A.push([x, y, 1, 0, 0, 0, -u * x, -u * y]);
    b.push(u);
    A.push([0, 0, 0, x, y, 1, -v * x, -v * y]);
    b.push(v);
  });
  const [a, bb, c, d, e, f, g, hh] = solve(A, b);
  // Column-major 4x4 with the z row/column as identity.
  return `matrix3d(${a},${d},0,${g}, ${bb},${e},0,${hh}, 0,0,1,0, ${c},${f},0,1)`;
};
