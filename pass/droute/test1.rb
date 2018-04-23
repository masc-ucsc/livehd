
# High level idea testing to see to check sat solver scalability


class SparseMatrix

  def initialize
    @hash = {}
  end

  def [](row, col)
    @hash[[row, col]]
  end

  def []=(row, col, val)
    @hash[[row, col]] = val
  end

end


class Pin
  attr_accessor :x
  attr_accessor :y
  attr_accessor :x_delta
  attr_accessor :y_delta

  def initialize(_x,_y)
    @x = _x
    @y = _y
    @x_delta = 0
    @x_delta = 0
  end
end

class Edge
  attr_accessor :id
  attr_accessor :src
  attr_accessor :dst
  @@ctr = 0

  def initialize(x1,y1,x2,y2)
    @id  = @@ctr.to_s(16)
    @@ctr = @@ctr + 1
    @src = Pin.new(x1,y1)
    @dst = Pin.new(x2,y2)
  end

  def to_s()
    return "#{@id} [#{@src.x},#{@src.y}] [#{@dst.x},#{@dst.y}]"
  end
end

$loc   = Array.new(16) { Array.new(16) }
$track = Array.new(16) { Array.new(16) }
$edges = Array.new

srand 100

10.times {
  loop do
    x1 = Random.rand(16)
    y1 = Random.rand(16)

    next if $loc[x1][y1] != nil

    x2 = Random.rand(16)
    y2 = Random.rand(16)
    x2 = x1 if Random.rand(16) < 5
    y2 = y1 if Random.rand(16) < 5

    next if x1 == x2 and y1 == y2
    next if $loc[x2][y2] != nil
    e = Edge.new(x1,y1,x2,y2)
    $edges << e
    $loc[x1][y1] = e
    $loc[x2][y2] = e
    break
  end
}

puts "Edges..."
$edges.each { |e|
  puts e.to_s()
}

16.times { |x|
  str = ""
  16.times { |y|
    if $loc[x][y] == nil
      str << " "
    else
      $track[x][y] = $loc[x][y].id
      str << $loc[x][y].id
    end
  }
  puts str
}


# 1st Connect all the trivial aligned 1 to 1

$edges.each { |e|
  if e.src.x == e.dst.x
    dst = e.src.y - e.dst.y
    i = e.dst.y
    if dst < 0
      i = e.src.y
      dst = -dst
    end
    i = i + 1 # skip itself
    dst.times { |d|
      break if $track[e.src.x][i+d] != nil
      $track[e.src.x][i+d] = e.id
    }
  elsif e.src.y == e.dst.y
    dst = e.src.x - e.dst.x
    i = e.dst.x
    if dst < 0
      i = e.src.x
      dst = -dst
    end
    i = i + 1 # skip itself
    dst.times { |d|
      puts "#{i} #{d} #{dst} tst" + e.to_s
      break if $track[i+d][e.src.y] != nil
      $track[i+d][e.src.y] = e.id
    }
  end
}

puts "Tracks..."

16.times { |x|
  str = ""
  16.times { |y|
    if $track[x][y] == nil
      str << " "
    else
      str << $track[x][y]
    end
  }
  puts str
}
