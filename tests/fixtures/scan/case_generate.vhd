-- expect: 4 architecture rtl
-- expect: 27 entity after_case_generate
-- case-generate with VHDL-2008 "end alt_label;".
architecture rtl of top is
begin
  g: case sel generate
    when a1: 0 =>
      signal s : bit;
    begin
    end a1;
    when a2: 1 | 2 =>
    end a2;
    when others =>
  end generate g;

  h: case sel generate
    when 0 =>
  end generate;

  blk: block is
    package inner is
    end package inner;
  begin
  end block blk;
end architecture;

entity after_case_generate is
end entity;
