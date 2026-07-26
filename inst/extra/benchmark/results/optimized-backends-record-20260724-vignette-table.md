| Operation | Records | reference | base | ALTREP | base × | ALTREP × |
|---|---:|---:|---:|---:|---:|---:|
| **Encoding and files** | | | | | | |
| str_read_lines | 1,000,000 | 1.03 s | 811 ms | 76 ms | 1.27 | 13.59 |
| str_view (escape) | 100,000 | 75 ms | 57 ms | 27 ms | 1.32 | 2.78 |
| str_conv | 100,000 | 37 ms | 35 ms | 18 ms | 1.06 | 2.06 |
| **Combine and duplicate** | | | | | | |
| str_dup | 100,000 | 43 ms | 42 ms | 4 ms | 1.02 | 10.75 |
| str_c | 100,000 | 35 ms | 32 ms | 4 ms | 1.09 | 8.75 |
| str_reverse | 100,000 | 39 ms | 38 ms | 8 ms | 1.03 | 4.88 |
| str_flatten | 1,000,000 | 100 ms | 91 ms | 37 ms | 1.10 | 2.70 |
| str_replace_na | 1,000,000 | 37 ms | 21 ms | 31 ms | 1.76 | 1.19 |
| **Substring** | | | | | | |
| str_sub<- | 100,000 | 37 ms | 33 ms | 4 ms | 1.12 | 9.25 |
| str_sub | 100,000 | 26 ms | 22 ms | 3 ms | 1.18 | 8.67 |
| str_sub_all<- | 100,000 | 69 ms | 38 ms | 8 ms | 1.82 | 8.62 |
| str_sub_all | 100,000 | 44 ms | 30 ms | 27 ms | 1.47 | 1.63 |
| **Trim and measure** | | | | | | |
| str_trim (left) | 1,000,000 | 364 ms | 345 ms | 42 ms | 1.06 | 8.67 |
| str_trim (right) | 1,000,000 | 344 ms | 336 ms | 40 ms | 1.02 | 8.60 |
| str_trim (both) | 1,000,000 | 203 ms | 199 ms | 45 ms | 1.02 | 4.51 |
| str_length | 1,000,000 | 60 ms | 57 ms | 49 ms | 1.05 | 1.22 |
| **Padding and layout** | | | | | | |
| str_pad (left) | 100,000 | 102 ms | 55 ms | 22 ms | 1.85 | 4.64 |
| str_pad (both) | 100,000 | 102 ms | 53 ms | 22 ms | 1.92 | 4.64 |
| str_pad (right) | 100,000 | 101 ms | 56 ms | 22 ms | 1.80 | 4.59 |
| str_width | 100,000 | 60 ms | 18 ms | 16 ms | 3.33 | 3.75 |
| str_wrap | 10,000 | 45 ms | 18 ms | 18 ms | 2.50 | 2.50 |
| **Case mapping** | | | | | | |
| str_to_lower | 100,000 | 47 ms | 42 ms | 12 ms | 1.12 | 3.92 |
| str_to_upper | 100,000 | 64 ms | 53 ms | 23 ms | 1.21 | 2.78 |
| str_to_title | 100,000 | 155 ms | 131 ms | 94 ms | 1.18 | 1.65 |
| **Fixed pattern** | | | | | | |
| str_replace (fixed) | 100,000 | 38 ms | 33 ms | 6 ms | 1.15 | 6.33 |
| str_count (fixed) | 1,000,000 | 100 ms | 38 ms | 22 ms | 2.63 | 4.55 |
| str_detect (fixed) | 1,000,000 | 42 ms | 20 ms | 12 ms | 2.10 | 3.50 |
| str_replace_all (fixed) | 100,000 | 49 ms | 37 ms | 14 ms | 1.32 | 3.50 |
| str_starts (fixed) | 1,000,000 | 35 ms | 19 ms | 12 ms | 1.84 | 2.92 |
| str_ends (fixed) | 1,000,000 | 35 ms | 19 ms | 12 ms | 1.84 | 2.92 |
| str_locate (fixed) | 1,000,000 | 68 ms | 35 ms | 25 ms | 1.94 | 2.72 |
| str_split (fixed) | 100,000 | 104 ms | 97 ms | 44 ms | 1.07 | 2.36 |
| str_extract (fixed) | 1,000,000 | 100 ms | 73 ms | 58 ms | 1.37 | 1.72 |
| str_extract_all (fixed) | 100,000 | 14 ms | 9 ms | 9 ms | 1.56 | 1.56 |
| str_locate_all (fixed) | 100,000 | 38 ms | 30 ms | 28 ms | 1.27 | 1.36 |
| **Regular expression** | | | | | | |
| str_extract (regex) | 100,000 | 104 ms | 60 ms | 39 ms | 1.73 | 2.67 |
| str_match (regex) | 100,000 | 63 ms | 52 ms | 26 ms | 1.21 | 2.42 |
| str_match_all (regex) | 100,000 | 226 ms | 198 ms | 117 ms | 1.14 | 1.93 |
| str_extract_all (regex) | 100,000 | 222 ms | 148 ms | 115 ms | 1.50 | 1.93 |
| str_replace (regex) | 100,000 | 115 ms | 94 ms | 63 ms | 1.22 | 1.83 |
| str_split (regex) | 100,000 | 164 ms | 140 ms | 98 ms | 1.17 | 1.67 |
| str_locate (regex) | 100,000 | 52 ms | 43 ms | 39 ms | 1.21 | 1.33 |
| str_replace_all (regex) | 100,000 | 144 ms | 138 ms | 109 ms | 1.04 | 1.32 |
| str_detect (regex) | 100,000 | 48 ms | 40 ms | 38 ms | 1.20 | 1.26 |
| str_locate_all (regex) | 100,000 | 126 ms | 110 ms | 104 ms | 1.15 | 1.21 |
| str_count (regex) | 100,000 | 83 ms | 75 ms | 75 ms | 1.11 | 1.11 |
| **Text boundary** | | | | | | |
| str_extract (boundary) | 100,000 | 65 ms | 46 ms | 27 ms | 1.41 | 2.41 |
| str_extract_all (boundary) | 100,000 | 203 ms | 194 ms | 114 ms | 1.05 | 1.78 |
| str_locate (boundary) | 100,000 | 48 ms | 30 ms | 28 ms | 1.60 | 1.71 |
| str_split (boundary) | 100,000 | 204 ms | 199 ms | 129 ms | 1.03 | 1.58 |
| str_count (boundary) | 100,000 | 79 ms | 77 ms | 74 ms | 1.03 | 1.07 |
| str_locate_all (boundary) | 100,000 | 121 ms | 116 ms | 116 ms | 1.04 | 1.04 |
| **Collation** | | | | | | |
| str_replace (coll) | 100,000 | 153 ms | 141 ms | 113 ms | 1.09 | 1.35 |
| str_count (coll) | 10,000 | 18 ms | 16 ms | 15 ms | 1.12 | 1.20 |
| str_replace_all (coll) | 10,000 | 20 ms | 19 ms | 17 ms | 1.05 | 1.18 |
| str_locate_all (coll) | 10,000 | 20 ms | 19 ms | 18 ms | 1.05 | 1.11 |
| str_locate (coll) | 100,000 | 118 ms | 111 ms | 107 ms | 1.06 | 1.10 |
| str_extract (coll) | 100,000 | 120 ms | 113 ms | 109 ms | 1.06 | 1.10 |
| str_split (coll) | 10,000 | 22 ms | 21 ms | 20 ms | 1.05 | 1.10 |
| str_detect (coll) | 100,000 | 115 ms | 109 ms | 105 ms | 1.06 | 1.10 |
| str_starts (coll) | 100,000 | 117 ms | 112 ms | 107 ms | 1.04 | 1.09 |
| str_extract_all (coll) | 10,000 | 19 ms | 18 ms | 18 ms | 1.06 | 1.06 |
| str_ends (coll) | 100,000 | 187 ms | 186 ms | 181 ms | 1.01 | 1.03 |
| **Order and compare** | | | | | | |
| str_equal | 1,000,000 | 78 ms | 71 ms | 59 ms | 1.10 | 1.32 |
| str_unique | 100,000 | 113 ms | 106 ms | 101 ms | 1.07 | 1.12 |
| str_order | 100,000 | 71 ms | 68 ms | 65 ms | 1.04 | 1.09 |
| str_rank | 100,000 | 79 ms | 74 ms | 73 ms | 1.07 | 1.08 |
