| Operation | Records | reference | base | ALTREP | base × | ALTREP × |
|---|---:|---:|---:|---:|---:|---:|
| **Encoding and files** | | | | | | |
| str_read_lines | 1,000,000 | 1.02 s | 798 ms | 74 ms | 1.28 | 13.81 |
| str_view (escape) | 100,000 | 73 ms | 60 ms | 31 ms | 1.22 | 2.35 |
| str_conv | 100,000 | 36 ms | 34 ms | 16 ms | 1.06 | 2.25 |
| **Combine and duplicate** | | | | | | |
| str_dup | 100,000 | 45 ms | 45 ms | 5 ms | 1.00 | 9.00 |
| str_c | 100,000 | 35 ms | 35 ms | 4 ms | 1.00 | 8.75 |
| str_reverse | 100,000 | 40 ms | 38 ms | 8 ms | 1.05 | 5.00 |
| str_flatten | 1,000,000 | 101 ms | 94 ms | 35 ms | 1.07 | 2.89 |
| str_replace_na | 1,000,000 | 36 ms | 21 ms | 32 ms | 1.71 | 1.12 |
| **Substring** | | | | | | |
| str_sub_all<- | 100,000 | 74 ms | 37 ms | 6 ms | 2.00 | 12.33 |
| str_sub | 100,000 | 27 ms | 24 ms | 3 ms | 1.12 | 9.00 |
| str_sub<- | 100,000 | 38 ms | 34 ms | 5 ms | 1.12 | 7.60 |
| str_sub_all | 100,000 | 46 ms | 31 ms | 28 ms | 1.48 | 1.64 |
| **Trim and measure** | | | | | | |
| str_trim (left) | 1,000,000 | 348 ms | 351 ms | 49 ms | 0.99 | 7.10 |
| str_trim (right) | 1,000,000 | 338 ms | 340 ms | 51 ms | 0.99 | 6.63 |
| str_trim (both) | 1,000,000 | 197 ms | 200 ms | 51 ms | 0.98 | 3.86 |
| str_length | 1,000,000 | 61 ms | 58 ms | 50 ms | 1.05 | 1.22 |
| **Padding and layout** | | | | | | |
| str_pad (both) | 100,000 | 100 ms | 53 ms | 20 ms | 1.89 | 5.00 |
| str_pad (left) | 100,000 | 99 ms | 52 ms | 20 ms | 1.90 | 4.95 |
| str_pad (right) | 100,000 | 99 ms | 52 ms | 20 ms | 1.90 | 4.95 |
| str_width | 100,000 | 60 ms | 17 ms | 16 ms | 3.53 | 3.75 |
| str_wrap | 10,000 | 45 ms | 19 ms | 17 ms | 2.37 | 2.65 |
| **Case mapping** | | | | | | |
| str_to_lower | 100,000 | 44 ms | 42 ms | 14 ms | 1.05 | 3.14 |
| str_to_upper | 100,000 | 63 ms | 53 ms | 25 ms | 1.19 | 2.52 |
| str_to_title | 100,000 | 150 ms | 129 ms | 96 ms | 1.16 | 1.56 |
| **Fixed pattern** | | | | | | |
| str_replace (fixed) | 100,000 | 38 ms | 36 ms | 6 ms | 1.06 | 6.33 |
| str_count (fixed) | 1,000,000 | 102 ms | 37 ms | 27 ms | 2.76 | 3.78 |
| str_split (fixed) | 100,000 | 107 ms | 100 ms | 38 ms | 1.07 | 2.82 |
| str_replace_all (fixed) | 100,000 | 48 ms | 44 ms | 18 ms | 1.09 | 2.67 |
| str_locate (fixed) | 1,000,000 | 70 ms | 40 ms | 31 ms | 1.75 | 2.26 |
| str_starts (fixed) | 1,000,000 | 35 ms | 23 ms | 16 ms | 1.52 | 2.19 |
| str_ends (fixed) | 1,000,000 | 36 ms | 25 ms | 17 ms | 1.44 | 2.12 |
| str_extract (fixed) | 1,000,000 | 101 ms | 76 ms | 55 ms | 1.33 | 1.84 |
| str_detect (fixed) | 1,000,000 | 42 ms | 33 ms | 23 ms | 1.27 | 1.83 |
| str_extract_all (fixed) | 100,000 | 15 ms | 10 ms | 9 ms | 1.50 | 1.67 |
| str_locate_all (fixed) | 100,000 | 38 ms | 32 ms | 29 ms | 1.19 | 1.31 |
| **Regular expression** | | | | | | |
| str_extract (regex) | 100,000 | 103 ms | 59 ms | 39 ms | 1.75 | 2.64 |
| str_match (regex) | 100,000 | 61 ms | 52 ms | 26 ms | 1.17 | 2.35 |
| str_split (regex) | 100,000 | 156 ms | 138 ms | 77 ms | 1.13 | 2.03 |
| str_extract_all (regex) | 100,000 | 217 ms | 144 ms | 112 ms | 1.51 | 1.94 |
| str_replace (regex) | 100,000 | 112 ms | 94 ms | 60 ms | 1.19 | 1.87 |
| str_match_all (regex) | 100,000 | 218 ms | 210 ms | 127 ms | 1.04 | 1.72 |
| str_replace_all (regex) | 100,000 | 143 ms | 135 ms | 108 ms | 1.06 | 1.32 |
| str_detect (regex) | 100,000 | 47 ms | 39 ms | 37 ms | 1.21 | 1.27 |
| str_locate (regex) | 100,000 | 52 ms | 44 ms | 41 ms | 1.18 | 1.27 |
| str_locate_all (regex) | 100,000 | 125 ms | 112 ms | 108 ms | 1.12 | 1.16 |
| str_count (regex) | 100,000 | 82 ms | 75 ms | 73 ms | 1.09 | 1.12 |
| **Text boundary** | | | | | | |
| str_extract (boundary) | 100,000 | 65 ms | 46 ms | 28 ms | 1.41 | 2.32 |
| str_locate (boundary) | 100,000 | 47 ms | 29 ms | 27 ms | 1.62 | 1.74 |
| str_split (boundary) | 100,000 | 198 ms | 195 ms | 128 ms | 1.02 | 1.55 |
| str_extract_all (boundary) | 100,000 | 199 ms | 195 ms | 150 ms | 1.02 | 1.33 |
| str_locate_all (boundary) | 100,000 | 119 ms | 114 ms | 110 ms | 1.04 | 1.08 |
| str_count (boundary) | 100,000 | 79 ms | 78 ms | 75 ms | 1.01 | 1.05 |
| **Collation** | | | | | | |
| str_replace (coll) | 100,000 | 148 ms | 141 ms | 113 ms | 1.05 | 1.31 |
| str_replace_all (coll) | 10,000 | 20 ms | 19 ms | 17 ms | 1.05 | 1.18 |
| str_split (coll) | 10,000 | 22 ms | 19 ms | 19 ms | 1.16 | 1.16 |
| str_extract (coll) | 100,000 | 118 ms | 107 ms | 104 ms | 1.10 | 1.13 |
| str_detect (coll) | 100,000 | 114 ms | 102 ms | 101 ms | 1.12 | 1.13 |
| str_count (coll) | 10,000 | 18 ms | 16 ms | 16 ms | 1.12 | 1.12 |
| str_locate (coll) | 100,000 | 117 ms | 105 ms | 104 ms | 1.11 | 1.12 |
| str_starts (coll) | 100,000 | 117 ms | 107 ms | 105 ms | 1.09 | 1.11 |
| str_locate_all (coll) | 10,000 | 20 ms | 18 ms | 18 ms | 1.11 | 1.11 |
| str_extract_all (coll) | 10,000 | 19 ms | 16 ms | 18 ms | 1.19 | 1.06 |
| str_ends (coll) | 100,000 | 185 ms | 177 ms | 177 ms | 1.05 | 1.05 |
| **Order and compare** | | | | | | |
| str_equal | 1,000,000 | 77 ms | 78 ms | 53 ms | 0.99 | 1.45 |
| str_order | 100,000 | 70 ms | 66 ms | 59 ms | 1.06 | 1.19 |
| str_rank | 100,000 | 77 ms | 74 ms | 65 ms | 1.04 | 1.18 |
| str_unique | 100,000 | 108 ms | 105 ms | 92 ms | 1.03 | 1.17 |
