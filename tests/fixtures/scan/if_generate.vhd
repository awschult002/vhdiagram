-- expect: 4 architecture rtl
-- expect: 28 entity after_if_generate
-- if-generate with elsif/else and VHDL-2008 alternative labels.
architecture rtl of top is
begin
  g1: if a: n = 1 generate
    signal s : bit;
  begin
  end a;
  elsif b: n = 2 generate
  end b;
  else c: generate
  end c;
  end generate g1;

  g2: if n > 3 generate
  elsif n < 0 generate
  else generate
  end generate;

  blk: block is
    package inner is
    end package inner;
  begin
  end block blk;
end architecture;

entity after_if_generate is
end entity;
